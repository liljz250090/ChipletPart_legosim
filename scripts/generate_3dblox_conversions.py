#!/usr/bin/env python3

import argparse
import math
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

import yaml


def parse_bool(value: str) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes"}


def maybe_number(text):
    if text is None:
        return None
    stripped = str(text).strip()
    if stripped == "":
        return None
    lowered = stripped.lower()
    if lowered in {"true", "false"}:
        return lowered == "true"
    try:
        integer = int(stripped)
        if "." not in stripped and "e" not in lowered:
            return integer
    except ValueError:
        pass
    try:
        return float(stripped)
    except ValueError:
        return stripped


def parse_block_definitions(path: Path):
    blocks = []
    with path.open() as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 5:
                raise ValueError(f"Malformed block definition line in {path}: {raw_line.rstrip()}")
            name, area_mm2, power_w, technology, is_memory = parts[:5]
            blocks.append(
                {
                    "name": name,
                    "area_mm2": float(area_mm2),
                    "power_w": float(power_w),
                    "technology": technology,
                    "is_memory": parse_bool(is_memory),
                }
            )
    return blocks


def parse_xml_records(path: Path):
    root = ET.parse(path).getroot()
    records = []
    for child in list(root):
        record = {}
        for key, value in child.attrib.items():
            record[key.replace("-", "_")] = maybe_number(value)
        records.append(record)
    return records


def parse_interconnects(path: Path):
    root = ET.parse(path).getroot()
    interconnects = []
    for index, net in enumerate(root.findall("net")):
        attrs = net.attrib
        interconnects.append(
            {
                "name": attrs.get("name", f"link_{index:04d}"),
                "type": attrs.get("type"),
                "source": attrs.get("block0"),
                "sink": attrs.get("block1"),
                "bb_count": maybe_number(attrs.get("bb_count")),
                "bandwidth_gbps": float(attrs.get("bandwidth", "0")),
                "average_bandwidth_utilization": float(
                    attrs.get("average_bandwidth_utilization", "0.5")
                ),
            }
        )
    return interconnects


def build_block_annotations(blocks, interconnects):
    used_io_types = defaultdict(set)
    incident_interconnects = defaultdict(list)
    for link in interconnects:
        for endpoint in ("source", "sink"):
            block = link.get(endpoint)
            if not block:
                continue
            if link.get("type"):
                used_io_types[block].add(link["type"])
            incident_interconnects[block].append(link["name"])

    annotations = {}
    for block in blocks:
        annotations[block["name"]] = {
            "source_schema": "ChipletPart",
            "block_record": block,
            "used_io_types": sorted(used_io_types.get(block["name"], set())),
            "incident_interconnects": incident_interconnects.get(block["name"], []),
        }
    return annotations


def build_dbv_document(blocks, block_annotations):
    chiplet_defs = {}
    for block in blocks:
        side = math.sqrt(block["area_mm2"]) * 1000.0
        side = round(side, 3)
        chiplet_defs[block["name"]] = {
            "type": "die",
            "design_area": [side, side],
            "seal_ring_width": [0.0, 0.0, 0.0, 0.0],
            "scribe_line_remaining_width": [0.0, 0.0, 0.0, 0.0],
            "thickness": 100.0,
            "shrink": 1.0,
            "tsv": False,
            "regions": {
                "core_reg": {
                    "side": "front",
                    "coords": [[0.0, 0.0], [side, 0.0], [side, side], [0.0, side]],
                }
            },
            "external": {"chipletpart_vendor_data": block_annotations[block["name"]]},
        }

    return {
        "Header": {"version": "2.0", "unit": "micron", "precision": 1000},
        "ChipletDef": chiplet_defs,
    }


def build_dataset(case_name: str, case_dir: Path):
    blocks = parse_block_definitions(case_dir / "block_definitions.txt")
    interconnects = parse_interconnects(case_dir / "block_level_netlist.xml")
    io_library = parse_xml_records(case_dir / "io_definitions.xml")
    layer_library = parse_xml_records(case_dir / "layer_definitions.xml")
    wafer_process_library = parse_xml_records(case_dir / "wafer_process_definitions.xml")
    assembly_process_library = parse_xml_records(case_dir / "assembly_process_definitions.xml")
    test_process_library = parse_xml_records(case_dir / "test_definitions.xml")

    dataset = {
        "blocks": blocks,
        "interconnects": interconnects,
        "io_library": io_library,
        "layer_library": layer_library,
        "wafer_process_library": wafer_process_library,
        "assembly_process_library": assembly_process_library,
        "test_process_library": test_process_library,
        "statistics": {
            "block_count": len(blocks),
            "interconnect_count": len(interconnects),
            "io_type_count": len(io_library),
            "layer_count": len(layer_library),
            "wafer_process_count": len(wafer_process_library),
            "assembly_process_count": len(assembly_process_library),
            "test_process_count": len(test_process_library),
        },
    }

    design_external = {
        "chipletpart_vendor_data": {
            "source_schema": "ChipletPart",
            "self_contained": True,
            "dataset": dataset,
            "modeling_assumptions": {
                "chiplet_type_mapping": "All original ChipletPart blocks are represented as synthetic 3dblox dies.",
                "chiplet_shape_mapping": "Each block is converted into a square die with side sqrt(area_mm2) * 1000 microns.",
                "region_mapping": "Each die uses one synthetic front-side region named core_reg.",
                "stack_mapping": "All dies are placed on a 2D shelf grid at z = 0 with orient = R0.",
                "connection_mapping": "No native 3dblox Connection section is emitted because the source testcase does not provide region-to-region physical connectivity.",
                "collateral_mapping": "No standard external collateral keys are populated because the source testcase does not include LEF/liberty/tech/deck collateral.",
            },
        }
    }

    return blocks, interconnects, design_external


def build_dbx_document(case_name: str, blocks, design_external):
    top_name = f"{case_name.upper()}Top"
    max_side = max(round(math.sqrt(block["area_mm2"]) * 1000.0, 3) for block in blocks)
    pitch = max(2500.0, math.ceil((max_side + 500.0) / 500.0) * 500.0)
    columns = max(1, math.ceil(math.sqrt(len(blocks))))

    chiplet_inst = {}
    stack = {}
    for index, block in enumerate(blocks):
        name = block["name"]
        chiplet_inst[name] = {"reference": name}
        stack[name] = {
            "loc": [float((index % columns) * pitch), float((index // columns) * pitch)],
            "z": 0.0,
            "orient": "R0",
        }

    return {
        "Header": {
            "version": "2.0",
            "unit": "micron",
            "precision": 1000,
            "include": [f"{case_name}.3dbv"],
        },
        "Design": {"name": top_name, "external": design_external},
        "ChipletInst": chiplet_inst,
        "Stack": stack,
    }


def write_yaml(path: Path, content):
    path.write_text(yaml.safe_dump(content, sort_keys=False, allow_unicode=False), encoding="utf-8")


def write_readme(path: Path, case_name: str):
    title = case_name.upper() if any(ch.isalpha() for ch in case_name) else case_name
    content = f"""# {title} 3dblox Conversion

This directory contains a self-contained 3dblox-style representation of the original `test_data/{case_name}` testcase, adjusted to more closely follow 3DBlox v2.0r1 conventions.

## Files
- `{case_name}.3dbv`: one `ChipletDef` per original ChipletPart block, modeled as a synthetic `die`, with block-level properties preserved under `external.chipletpart_vendor_data`
- `{case_name}.3dbx`: one `ChipletInst` per original ChipletPart block, with placement data emitted in a separate `Stack` section and the full testcase dataset preserved under `Design.external.chipletpart_vendor_data.dataset`

## What Is Preserved
- Every original block record is preserved as structured data.
- Every original interconnect record is preserved as structured data.
- The IO library, layer library, wafer-process library, assembly-process library, and test-process library are all preserved as structured data.
- The converted testcase no longer depends on separate sidecar source definitions.
- Vendor-specific ChipletPart metadata is namespaced inside `external.chipletpart_vendor_data` so it does not collide with standard 3dblox collateral keys.
- Standard collateral keys such as `liberty_file`, `LEF_file`, and `APR_tech_file` are intentionally left unset because the source testcase does not provide those artifacts.

## Current Modeling Assumptions
- Each original block is modeled as a square chiplet with side `sqrt(area_mm2) * 1000` microns.
- Each synthetic chiplet is emitted as `type: die`.
- The generated `3dbv` uses one synthetic front-side region named `core_reg` per chiplet because the original testcase does not provide bump maps or native 3D region connectivity.
- The generated `3dbx` keeps a simple 2D shelf-grid placement in the `Stack` section and does not add a `Connection` section because the source testcase is logical/cost-model oriented rather than native 3dblox physical assembly data.
"""
    path.write_text(content, encoding="utf-8")


def convert_case(case_dir: Path, output_root: Path):
    case_name = case_dir.name
    blocks, interconnects, design_external = build_dataset(case_name, case_dir)
    block_annotations = build_block_annotations(blocks, interconnects)

    out_dir = output_root / f"{case_name}_3dblox"
    out_dir.mkdir(parents=True, exist_ok=True)

    write_yaml(out_dir / f"{case_name}.3dbv", build_dbv_document(blocks, block_annotations))
    write_yaml(out_dir / f"{case_name}.3dbx", build_dbx_document(case_name, blocks, design_external))
    write_readme(out_dir / "README.md", case_name)

    return out_dir


def main():
    parser = argparse.ArgumentParser(description="Convert ChipletPart testcases into self-contained 3dblox directories.")
    parser.add_argument(
        "--test-data-root",
        type=Path,
        default=Path("/home/liljz25/ChipletPart-new/test_data"),
        help="Root directory containing testcase folders.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path("/home/liljz25/ChipletPart-new/new"),
        help="Root directory to receive *_3dblox outputs.",
    )
    parser.add_argument(
        "--cases",
        nargs="*",
        help="Optional list of testcase directory names to convert. Defaults to all except ga100.",
    )
    args = parser.parse_args()

    if args.cases:
        case_dirs = [args.test_data_root / name for name in args.cases]
    else:
        case_dirs = sorted(
            path
            for path in args.test_data_root.iterdir()
            if path.is_dir() and path.name not in {"ga100"} and path.name != "README.md"
        )

    for case_dir in case_dirs:
        if not case_dir.is_dir():
            raise FileNotFoundError(f"Testcase directory not found: {case_dir}")
        out_dir = convert_case(case_dir, args.output_root)
        print(f"[OK] {case_dir.name} -> {out_dir}")


if __name__ == "__main__":
    main()
