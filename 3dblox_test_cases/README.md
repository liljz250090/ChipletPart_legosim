# 3dblox Conversion Outputs

This directory collects self-contained 3dblox-style conversions of ChipletPart testcases.

## Per-testcase directories
- `ga100_3dblox/`
- `epyc7282_3dblox/`
- `48_1_14_4_1600_1600_3dblox/`
- `48_2_14_4_1600_1600_3dblox/`
- `48_4_14_4_1600_1600_3dblox/`
- `48_8_14_4_1600_1600_3dblox/`

Each `*_3dblox/` directory contains:
- `<case>.3dbv`
- `<case>.3dbx`
- `README.md`

## Legacy / companion files
- `ga100.3dbv`, `ga100.3dbx`: earlier top-level GA100 conversion copies
- `ga100_odb.3dbv`, `ga100_odb.3dbx`, `ga100.odb`, `ga100_min_tech.lef`, `export_ga100_odb.tcl`: ODB-oriented GA100 artifacts created for OpenROAD/OpenDB experiments
- `ga100.partitioned.odb`: writeback example generated during ODB partition annotation testing

## Notes
- All testcase conversions are self-contained and preserve the original ChipletPart block, interconnect, IO, layer, wafer-process, assembly-process, and test-process data inside `external.chipletpart_vendor_data`.
- The generated 3dblox models use synthetic `die` chiplets with a single front-side region `core_reg` and a simple 2D shelf-grid `Stack`.
