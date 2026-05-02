> [!WARNING]
> THIS LIBRARY IS A WORK IN PROGRESS! ANYTHING CAN CHANGE AT ANY MOMENT WITHOUT ANY NOTICE! USE THIS LIBRARY AT YOUR OWN RISK!

# wlclient

A minimal Wayland client library for building native Linux GUI applications. It is written in C and currently built as a shared and static library via `CMake`, with the long-term goal of becoming a single-header, drop-in, `stb`-style library, if possible. At the very least, the `CMake` requirement will be dropped.

The core idea is to make it simple to put graphics on the screen under Wayland.

The scope is deliberately narrow: own the window, own the input, own the client-side decorations, and hand rendering off to a user-chosen backend (EGL/OpenGL today, Vulkan later, or a custom one).

## The problem

This has been notoriously complicated because of the number of compositor implementations, the lack of documentation about version support for specific features across different compositors, and the refusal of some compositor vendors to support compositor-side window decorations.

The most commonly used solution, `libdecor`, introduces multiple dependencies and a combinatorial explosion of rendering paths: one using Cairo, one using a fallback implementation, one using GTK, and others. In my opinion, this is already too much.

Even `GLFW`, a widely used cross-platform library for creating windows and handling input, has three separate approaches: one that uses `libdecor`, one that uses `client-side decorations` (CSD), and one fallback path that does not support all features.

I refuse to believe that this is the only way to perform basic tasks such as opening a window, rendering graphics, and handling user input.

**Design goals and philosophy**
* **Zero Allocation Initialization (ZAI not RAII)**. For most resources in the library, it makes sense to use static arrays (how many applications need a dynamic number of windows?). There is a fixed number of windows, input devices, framebuffers, etc. It is planned to support configuring these through compile-time constants. All places in the codebase that allocate memory do so through the `wlclient_allocator`. </br> Unfortunately, there is a dependency on the `wayland-client` library, which does not give user code control over memory allocations. This is largely out of scope, but the long-term plan is to replace `wayland-client` with a custom implementation. The problem is that this is a very tedious and long-term project.
* **There are exactly two approaches for decorations** - either you use client-side decorations (CSD), or your compositor supports the decoration-rendering extension (SSD). There is exactly one branch for client-side decoration rendering, and it is (or will be) well tested across all major compositors.
* That said, the protocol is still complicated, and a best effort will be made to support as many mainstream compositors as possible. This is not easy and introduces its own combinatorial explosion in order to support newer features while still providing fallbacks for certain cases. **The main goal is to reduce these branches as much as possible while remaining compatible with most mainstream compositors**.
* There will be three built-in rendering backends: EGL, Vulkan, and SHM (software). There will be an API to implement custom rendering backends if these do not meet your needs. **BUT most importantly** these are plugins and not somthing that is forced.

## Prototype features set

- Creates XDG toplevel windows directly on `wl_compositor` + `xdg_wm_base`, no wrapper libraries.
- **Client-side decorations**: title bar subsurface + four edge subsurfaces, rendered via `wl_shm`.
- **Interactive resize** via `xdg_toplevel_resize` dispatched from edge surfaces, with corner detection on the top/bottom edges.
- Pointer input: enter/leave, motion, button, frame-batched dispatch — a `wl_pointer.frame` acts as the single dispatch barrier so motion + button in the same frame see a consistent coordinate snapshot.
- Keyboard input: focus, raw Wayland keycodes, XKB keysyms, stable modifier flags, repeat info, UTF-8 text, and compose/dead-key handling.
- Pluggable allocator (`wlclient_allocator`) for embedded / custom-heap use.

**Basic decoration rendering, drag and resize features**

![Demo GIF](./docs/demo_basic_drag_and_resize_features.gif)

## Feature status

Features that are next in the development pipeline:

| High Priority Feature                        | Status      | Notes       |
| -------------------------------------------- | ----------- | ----------- |
| Keyboard input                               | Done        | Raw keycodes, XKB keysyms, stable modifier flags, repeat info, focus, UTF-8 text, and compose/dead-key handling. Key repeat generation is left to user code. |
| Pointer axis (scroll)                        | In Progress |             |
| Window minimize                              | In Progress |             |
| Window maximize / fullscreen                 | In Progress |             |
| Cursor (setting, animating, size changes)    | Planned     |             |
| Fractional scaling (`wp-fractional-scale`)   | Planned     |             |
| Clipboard                                    | Planning    |             |
| Window visibility (`SUSPENDED` state)        | Done        |             |
| Image loading (`stb_image`)                  | Planning    | Likely needed for loading custom cursor icons |
| Decoration opacity / alpha blending          | Planning    | Currently broken |

Features that depend on the above being finished:

| Feature                                      | Classification | Notes       |
| -------------------------------------------- | -------------- | ----------- |
| Compositor side decoration (SSD) rendering   | Important      | The decoration extension should be used on compositors that support it. |
| Decoration buttons for hide/show/minimize    | Important      | Probably should have configurable icons. |
| Multi-monitor support                        | Important      | Enumeration, features querying, connect/disconnect events, per-monitor scale factor, etc. |
| Extensive multi-window setup testing         | Necessary      | Automated testing for multi-window scenarios |
| Double click support                         | Nice to have   | For both mouse and keyboard buttons |
| Window icon                                  | Nice to have   | Should be easy to add support for window icons after this is done for cursors |
| Font rendering (`stb_truetype`)              | Nice to have   | Likely needed if window decorations are to render a title |
| Joystick support                             | Nice to have   | |
| Touch support                                | Nice to have   | |

Plans for backend support:

| Backend                     | Status       | Details |
| --------------------------- | ------------ | ------- |
| EGL                         | Done         |         |
| Vulkan                      | Planned      |         |
| SHM                         | Planned      | Software based rendering |
| Testing backend             | Planned      | A headless backend for automated rendering tests would be useful |
| Testing compositor          | No plan yet  | This is the best way to test a library like this, but it requires significantly more work |

## Compositor support

(TBA)

The library is being developed under GNOME 49.5 Mutter (Wayland). It's currently the only compositor that has being test with the existing feature set.

Extensive testing per compositor is part of the roadmap (KWin, Sway, Hyprland, Weston..).

Tests running per compositor with the help of virtualization will be experimented with.

## Building

To build the example code run the following:

```sh
cmake -B build
cmake --build build -j
./build/sandbox
```

Runtime dependencies: `libwayland-client`, `libxkbcommon`, `libwayland-egl`, `libwayland-cursor`, EGL, OpenGL.

## Design differences from GLFW and SDL

### vs. GLFW

- **Pointer events are frame-batched, not per-event.** GLFW fires a callback per `wl_pointer` event because its API is an X11/Win32-era synchronous-callback contract. `wlclient` dispatches at `wl_pointer.frame`, so motion coalesces into a single call per frame and button presses see the final motion coordinates. Matches how the protocol was designed.
- **Compositor-driven visibility.** GLFW's `iconify` callback does not fire on Wayland — it's a known limitation because GLFW doesn't bind `xdg_wm_base` v6 and map `SUSPENDED` state back into its API. `wlclient` tracks the `SUSPENDED` state internally and avoids unnecessary buffer swaps while suspended.
- **No hidden global state for the window system.** GLFW has implicit global init; `wlclient` requires an explicit `wlclient_init` / `wlclient_shutdown` pair and a user-supplied allocator.
- **Client-side decorations are the library's problem, not the user's.** GLFW relies on `zxdg_decoration_manager`, or `libdcor`, or a fallback. `wlclient` does not use `libdcor`.

### vs. SDL

- **No renderer abstraction.** SDL ships a 2D renderer and a GPU abstraction. `wlclient` delegates rendering entirely — you call GL (or Vulkan later) yourself. The library only owns the surface and buffer lifecycle.
- **No audio / gamepad / filesystem / threading helpers.** SDL is a platform abstraction layer; `wlclient` is just a windowing layer. Everything else is out of scope.
- **No cross-platform pretense.** SDL runs on Windows, macOS, mobile, consoles. `wlclient` is Wayland-only. No `X11` fallback, no `DRM/KMS` mode. If you need X11, use something else.
