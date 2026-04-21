TODO: OUT OF DATE!

I need to design the code to be testable by configuring a test output file. This way I can run an integration test and verify the color output.

## Size Naming Policy

Logical sizes are the source of truth. Pixel sizes are derived from logical sizes and the current scale.

- `window_*` means the full outer window bounds. It includes the content area, the top decoration, and all four edge regions.
- `frame_*` means the non-content chrome as a whole. It is the combination of the decoration and the edge regions.
- `content_*` means the client drawable area inside the frame.
- `decoration_*` means the top decoration only.
- `edge_*` means one of the four edge regions. Per-edge names should use `edge_top_*`, `edge_bottom_*`, `edge_left_*`, and `edge_right_*`.
- `logical_*` means a size or position in surface-local units before scaling is applied.
- `pixel_*` means a size in buffer pixels after applying the current surface scale and rounding to an integer.

Derived:
- `visible_*` means a derived value that depends on whether optional chrome is currently shown. This is used for values that change when decoration visibility changes.

![window](./window_areas.svg)
