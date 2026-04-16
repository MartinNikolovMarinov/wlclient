# Event Polling & Timer Utils: Bug Report

## Wayland Protocol Compliance Issues

### 2. Missing POLLERR/POLLHUP handling in poll loop (wl-client.c:422-440)

```c
if (pfds[DISPLAY_FD].revents & POLLIN) {
    // ...
}
else {
    wl_display_cancel_read(g_state.display);
}
```

When the compositor disconnects or the fd errors, `poll` returns with `POLLERR` or `POLLHUP`
set in `revents` but not `POLLIN`. The code enters the `else` branch, calls `cancel_read`,
and loops back. The next iteration calls `prepare_read`, then `display_flush` on a dead fd,
which fails, and then cancels read again. This spin continues until flush finally returns
an error.

The result is delayed error detection, wasted cycles, and confusing error diagnostics.

**Fix:** Check for `POLLERR | POLLHUP` explicitly after poll returns and treat them as
connection errors.

---

### 3. Timeout returns WLCLIENT_ERROR_OK instead of WLCLIENT_ERROR_EVENT_POLL_TIMEOUT (wl-client.c:413-415, types.h:49)

```c
else if (poll_result.timedout) {
    wl_display_cancel_read(g_state.display);
    goto done_ok;  // returns WLCLIENT_ERROR_OK
}
```

`WLCLIENT_ERROR_EVENT_POLL_TIMEOUT` exists in the enum but is never used. The caller cannot
distinguish "events were dispatched" from "timeout expired with no events." This makes it
impossible for the application to implement idle behaviors or frame-pacing logic based on
whether input was actually received.

**Fix:** Return `WLCLIENT_ERROR_EVENT_POLL_TIMEOUT` on the timeout path.

---

### 4. Outgoing requests not flushed after dispatching from pre-read queue (wl-client.c:388-400)

```c
while (wl_display_prepare_read(g_state.display) != 0) {
    i32 ret = wl_display_dispatch_pending(g_state.display);
    if (ret > 0) {
        received_event = true;
        break;      // <-- exits without ever calling display_flush
    }
}
if (received_event) goto done_ok;
```

If `dispatch_pending` fires listeners that generate outgoing requests (e.g., `xdg_wm_base_pong`
in response to a ping, or `xdg_surface_ack_configure`), those requests sit in the outgoing
buffer unflushed. They won't be sent to the compositor until the *next* call to
`wlclient_poll_events`.

For `xdg_wm_base_pong`, compositors have a timeout. If the application's event loop is slow,
the delayed pong could cause the compositor to consider the client unresponsive and kill it.

**Fix:** Call `display_flush` before `goto done_ok` when `received_event` is true from the
dispatch_pending path.

---

### 5. Scale factor change does not trigger backend resize (wl-client.c:1120-1132)

```c
static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor) {
    // ...
    wdata->scale_factor = (f32)factor;
    wl_surface_set_buffer_scale(surface, (i32) wdata->scale_factor);
    // FIXME: what else needs to be notified here ?
}
```

When the preferred buffer scale changes at runtime (e.g., dragging a window between monitors
with different DPI), the scale factor is updated and `wl_surface_set_buffer_scale` is called,
but:
- `framebuffer_pixel_width/height` are NOT recalculated
- The backend resize callback is NOT invoked
- The EGL window is NOT resized

The client renders at the old buffer size with the new scale factor, producing incorrectly
sized output (usually a quarter of the window is drawn, the rest is garbage).

**Fix:** Recalculate `framebuffer_pixel_*` and call `backend_resize_window` after updating the
scale factor, the same way `xdg_surface_configure` does.

---

### 6. `xdg_toplevel.configure` states array is entirely ignored (wl-client.c:878)

```c
(void)states;
```

The states array communicates whether the window is maximized, fullscreen, resizing, focused,
tiled, suspended, etc. Without parsing this, the client:
- Cannot know if it is fullscreen (should hide decorations, fill entire output)
- Cannot know if it is maximized (should not draw resize edges)
- Cannot know if it is activated/focused (should change title bar appearance)
- Cannot adapt rendering for the tiled state

Per the xdg-shell spec, "The states listed in the event specify how the width/height arguments
should be interpreted." Ignoring states means the width/height are not interpreted correctly
for maximized/fullscreen states.

---

## Timer / Utility Bugs

### 7. `timeout_ns == 0` means "block forever" --- inverted convention (wl-utils.c:78)

```c
const bool is_blocking = (nanoseconds == 0);
```

In every standard POSIX polling API (`poll`, `ppoll`, `select`, `epoll_wait`), timeout=0 means
"return immediately without blocking" and timeout=-1 (or NULL timespec) means "block forever."
This function inverts that convention: 0 blocks forever, and there is no way to do a
non-blocking poll at all.

A caller writing `wlclient_poll_events(0)` expecting a non-blocking check will get an
infinite block.

**Fix:** Use a sentinel value like `UINT64_MAX` for "block forever" and treat 0 as
"non-blocking poll."

---

### 8. `nanosleep` not retried on EINTR during CPU frequency calibration (wl-utils.c:50)

```c
nanosleep(&sleepTime, NULL);
```

If a signal interrupts the sleep, `nanosleep` returns early. The second argument (remaining
time pointer) is NULL, so the remaining time is lost. The measured elapsed time will be
shorter than intended, and the TSC frequency will be overestimated (fewer nanoseconds elapsed
for the same number of TSC ticks).

**Fix:** Pass a `remaining` timespec and retry the sleep in a loop until it completes or the
remaining time is zero.

---

### 9. `wlclient_get_cpu_frequency_hz` returns 0 on unsupported architectures (wl-utils.c:37-63)

On architectures other than x86_64 and ARM64, `frequency` stays 0 and is returned. Any code
that divides by the frequency (to convert ticks to time) will trigger a division by zero.
`wlclient_get_perf_counter()` correctly falls back to monotonic clock on unknown architectures,
but the frequency function has no corresponding fallback.

**Fix:** Return `WLCLIENT_SECOND` (1,000,000,000) as the fallback, since the perf counter
fallback returns nanoseconds, making the "frequency" effectively 1 GHz.

---

## Other Issues

### 10. Fractional scale factor silently truncated to integer (wl-client.c:264-265, 980-981)

```c
wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
```

The `(u32)` cast applies to `scale_factor` alone (operator precedence), truncating 1.5 to 1,
1.25 to 1, etc. This isn't just a "fractional scaling not implemented" situation --- the code
actively destroys fractional information. When fractional scaling support is added, this cast
must be replaced with proper rounding of the final product, not truncation of the scale factor.

There is a TODO acknowledging this, but the current expression is wrong even as a placeholder
because it silently produces incorrect results without any warning or assertion.

---

### 11. `display_flush` inner error path relies on accidental correctness (wl-client.c:670-691)

When the inner `poll` loop fails with a fatal errno:

```c
while (poll(&fd, 1, -1) < 0) {
    if (errno != EINTR && errno != EAGAIN) {
        res = false;
        break;  // breaks inner loop only
    }
}
// falls through to:
if (!(fd.revents & POLLOUT)) {  // fd.revents == 0 (initialized, never set by failed poll)
    res = false;
    break;  // breaks outer loop
}
```

The outer loop exits only because `fd.revents` was initialized to 0 and poll's failure
didn't set it. The code works, but the control flow is fragile. A refactor that changes the
initialization or adds another fd could break this accidentally-correct behavior.

**Fix:** After the inner loop, check whether `res` is already false and break from the outer
loop explicitly.

