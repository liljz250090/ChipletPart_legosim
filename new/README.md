# GA100 3dblox Conversion

This directory contains a self-contained 3dblox-style representation of the original `test_data/ga100` testcase, adjusted to more closely follow 3DBlox v2.0r1 conventions.

## Files
- `ga100.3dbv`: one `ChipletDef` per original ChipletPart block, modeled as a synthetic `die`, with block-level properties preserved under `external.chipletpart_vendor_data`
- `ga100.3dbx`: one `ChipletInst` per original ChipletPart block, with placement data emitted in a separate `Stack` section and the full testcase dataset preserved under `Design.external.chipletpart_vendor_data.dataset`

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
