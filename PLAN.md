# Decoration Plan

## Two Approaches

### 1. Separate Decoration Surface

Create a second Wayland surface for the window decoration, most likely a top bar. The library owns this surface, renders it internally with software rendering through `wl_shm`, and keeps it synchronized with the main content surface.

### 2. Internal Draw Commands

Represent decorations as an internal list of rendering commands, similar in spirit to immediate-mode UI systems. Those commands are then consumed by a backend for OpenGL, Vulkan, or software rendering, and the backend draws the decoration into the main window content.

## Problem

The library owns windowing and input handling, while the user owns the rendering pipeline. The main unresolved design question is how the library should draw client-side window decorations without taking ownership of the user's renderer.

This matters because the decoration is still part of the library's responsibility. The library tracks input events, hit testing, movement, resize logic, and the window frame behavior. The open question is only how the decoration pixels are produced.

## Context

If the library assumes a single rendering API such as OpenGL, it can render the top bar itself with internal shaders. That is a common pattern in immediate-mode GUI systems.

However, that approach becomes problematic if the user wants to use another backend such as Vulkan. In that case, decoration rendering tied to an EGL or GL pipeline leaks rendering ownership back into the library.

That is why the decoration problem should be treated as an architecture boundary problem, not just as a drawing problem.

## Approach 1: Separate Decoration Surface

In this model:

- The main surface is used for application content.
- The user renders the main content with whatever backend they choose.
- The library creates and manages a second surface for the decoration.
- The decoration surface is rendered internally by the library using software rendering.

### Why this is attractive

- It keeps the ownership boundary clean.
- It does not force OpenGL into the library design.
- It works with Vulkan, EGL, and other future backends.
- The library remains responsible for window chrome.

### Main drawbacks

- The library must manage multiple coordinated surfaces.
- Resize and commit synchronization become more complex.
- Pointer and other input events must be handled across both surfaces.
- The library must avoid visual seams and scaling issues.

### Summary

This approach increases Wayland-specific complexity, but that complexity is local and well aligned with the problem being solved.

## Approach 2: Internal Draw Commands

In this model:

- The library defines an internal list of commands for decoration rendering.
- Backends consume those commands and render the decoration into the main content path.
- The same decoration description could theoretically be rendered by GL, Vulkan, or software backends.

### Why this is attractive

- It keeps the decoration inside a single main window rendering flow.
- It can support multiple graphics backends in a unified way.
- It resembles how immediate-mode GUI systems commonly work.

### Main drawbacks

- The library starts to become a rendering framework, not just a windowing layer.
- It must define and maintain backend interfaces.
- It likely needs custom shaders, clipping, batching, text/icon handling, and state rules.
- Backend support becomes an ongoing maintenance burden.

### Summary

This approach has a higher ceiling, but it significantly expands the scope of the library.

## Recommendation

The current recommendation is to prefer the separate decoration surface.

The reason is not that it is universally simpler, but that it preserves the intended ownership split:

- library owns windowing
- library owns decorations
- user owns rendering

The draw-command approach is better suited to a library that wants to become a full rendering abstraction or UI framework. That does not appear to be the current goal.

## Practical Direction

The likely next direction is:

- keep the main content surface fully user-owned
- add a library-owned decoration surface
- render the decoration in software with `wl_shm`
- let the library continue to own hit testing and frame behavior

This leaves room to revisit a backend-agnostic draw-command system later if the project grows in that direction.
