# wl-client.c Refactor Review

Review target: `src/wl-client.c` (commit e9142ff, status: `M`).
Scope: global-state init/shutdown path, registry binding, seat/input handling,
xdg_wm_base ping handling. No window/surface/buffer code exists yet.

System reference versions (from `/usr/share/wayland/wayland.xml`, Wayland
1.24.0):

| interface         | version |
|-------------------|---------|
| wl_compositor     | 6       |
| wl_subcompositor  | 1       |
| wl_shm            | 2       |
| wl_seat           | 10      |
| wl_pointer        | 10      |
| wl_keyboard       | 10      |
| xdg_wm_base       | 7       |

The client binds with `min(wl_*_interface.version, server_version)`, so on this
system the effective versions will sit at or near these maxima against any
modern compositor.

Findings are grouped by severity. "Protocol violation" means the code actually
breaks the wire protocol as written in wayland.xml / xdg-shell.xml.

---

## 1. Protocol violations

### 1.1 `wl_seat_destroy` used in place of `wl_seat_release`

Location: `src/wl-client.c:212`, inside `destroy_input_device`.

```c
if (input_device->seat) wl_seat_destroy(input_device->seat);
```

`wl_seat.release` is a destructor request added in version 5. Since wayland
1.24 advertises `wl_seat` at version 10 and the client binds with
`min(wl_seat_interface.version, version)`, the effective bound version is
essentially always ≥ 5 on any modern compositor. When a destructor request
exists, clients are expected to use it so the server-side object is torn down
cleanly; `wl_seat_destroy` is only a client-side proxy free and does **not**
notify the server.

Protocol source — wl_seat.release, `since="5"`:
<https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml#L2014>
(local: `/usr/share/wayland/wayland.xml:2014`)

Fix: branch on `wl_proxy_get_version((struct wl_proxy*) seat) >= 5` and call
`wl_seat_release` when supported, `wl_seat_destroy` otherwise.

### 1.2 `wl_shm_destroy` used in place of `wl_shm_release`

Location: `src/wl-client.c:148`, inside `wlclient_shutdown`.

```c
if (g_state.shm) wl_shm_destroy(g_state.shm);
```

`wl_shm.release` is a destructor request added in version 2. Wayland 1.24
advertises `wl_shm` at version 2, so on this system the bound version will be
2, and the protocol-correct cleanup is `wl_shm_release`.

Protocol source — wl_shm.release, `since="2"`:
<https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml#L463>
(local: `/usr/share/wayland/wayland.xml:463`)

Fix: same version-check pattern as 1.1, guarded on version ≥ 2.

### 1.3 `wl_pointer_release` / `wl_keyboard_release` called without version guard

Location: `src/wl-client.c:209-210`, inside `destroy_input_device`, and
`src/wl-client.c:425, 459`, inside `seat_capabilities`.

```c
if (input_device->pointer)  wl_pointer_release(input_device->pointer);
if (input_device->keyboard) wl_keyboard_release(input_device->keyboard);
```

Both `wl_pointer.release` and `wl_keyboard.release` are destructors added in
version 3 of their respective interfaces. The bound version of wl_pointer /
wl_keyboard is inherited from the parent wl_seat version. For seats bound at
version 1 or 2 the `release` opcode does not exist in the protocol for those
child objects, and issuing it is a protocol error.

In practice all mainstream compositors (Sway, KWin, Mutter, Weston, Hyprland)
expose wl_seat at ≥ 5, so this is latent rather than active. It is still a
real violation because the code unconditionally issues the request with no
version check — a minimal compositor or older one would break it.

Protocol sources:
- wl_pointer.release, `since="3"`:
  <https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml#L2186>
  (local: `/usr/share/wayland/wayland.xml:2186`)
- wl_keyboard.release, `since="3"`:
  <https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml#L2589>
  (local: `/usr/share/wayland/wayland.xml:2589`)

Fix: check `wl_proxy_get_version((struct wl_proxy*) pointer) >= 3` before
calling release; fall back to `wl_pointer_destroy` / `wl_keyboard_destroy`
otherwise.

### 1.4 Release/destroy requests never flushed before `wl_display_disconnect`

Location: `src/wl-client.c:145-150`, inside `wlclient_shutdown`.

```c
if (g_state.compositor) wl_compositor_destroy(g_state.compositor);
if (g_state.subcompositor) wl_subcompositor_destroy(g_state.subcompositor);
if (g_state.xdgWmBase) xdg_wm_base_destroy(g_state.xdgWmBase);
if (g_state.shm) wl_shm_destroy(g_state.shm);
if (g_state.registry) wl_registry_destroy(g_state.registry);
if (g_state.display) wl_display_disconnect(g_state.display);
```

Per `wayland-client-core.h` (lines 70–73):

> When a wl_proxy marshals a request, it will write its wire representation to
> the display's write buffer. The data is sent to the compositor when the
> client calls wl_display_flush().

`wl_display_disconnect` does **not** flush the client-side write buffer. Any
destructor requests issued during shutdown (`xdg_wm_base_destroy`, release
requests added per findings 1.1–1.3, any future surface / buffer destructors)
can sit in the write buffer and never reach the server. The server will then
clean them up when the socket closes, which is sloppy but works for
ephemeral state — it becomes actively wrong when shared resources (buffers
held by the compositor, persisted state) are involved.

Fix: call `wl_display_flush(g_state.display)` (or `wl_display_roundtrip` for
full synchronization) immediately before `wl_display_disconnect`.

---

## 2. Logic bugs

### 2.1 Init requires every seat to have both pointer and keyboard

Location: `src/wl-client.c:117-128`, inside `wlclient_init`.

```c
ENSURE_OR_GOTO_ERR(g_state.input_devices_count > 0);
for (i32 i = 0; i < g_state.input_devices_count; i++) {
    wlclient_input_device* d = &g_state.input_devices[i];
    ENSURE_OR_GOTO_ERR(d->used);
    ENSURE_OR_GOTO_ERR(d->seat);
    ENSURE_OR_GOTO_ERR(d->seat_id);
    ENSURE_OR_GOTO_ERR(d->pointer);
    ENSURE_OR_GOTO_ERR(d->keyboard);
}
```

Per wl_seat spec (`wayland.xml:1897`), the capability bitmask is any subset of
`{pointer, keyboard, touch}`. Valid configurations include:

- touch-only seats (tablets, kiosks)
- keyboard-only seats (headless SSH forwarding, virtual seats)
- pointer-only seats (barcode scanners, some multi-seat setups)
- seats with **zero** capabilities (a hot-plugged seat whose devices haven't
  come up yet, or a seat whose devices were just unplugged — the compositor
  sends `capabilities = 0`)

The code also rejects init when `input_devices_count == 0`, which breaks
headless / virtual compositors and Weston `--backend=headless` test setups.

Protocol sources:
- wl_seat.capability enum (`wayland.xml:1897-1905`):
  <https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml#L1897>
- wl_seat.capabilities event (`wayland.xml:1915-1944`) describes add/remove
  semantics and explicitly allows capabilities to toggle.

Fix: drop the hard check. Require at most the capabilities the application
actually needs, ideally none at init time; attach pointer/keyboard on demand
when a seat advertises the corresponding capability.

### 2.2 `free()` used on memory allocated via a custom allocator

Location: `src/wl-client.c:211` (destroy) and `src/wl-client.c:485` (allocate).

```c
// destroy
if (input_device->seat_name) free(input_device->seat_name);
// allocate
input_device->seat_name = g_state.allocator.strdup(name);
```

`wlclient_allocator` (`include/types.h:60-63`) exposes `alloc` and `strdup`
but no `free`. The shutdown path unconditionally uses libc `free`, which is
only valid if `strdup` came from the libc heap. Any custom allocator (arena,
pool, tracing malloc, sanitizer interposer) that uses a different heap
allocator silently corrupts state.

This is both a design gap (the allocator API is incomplete) and an active
bug for anyone who passes a non-default allocator. The default path is safe
by accident.

Fix: add `void (*free)(void*)` to `wlclient_allocator`, default it to libc
`free`, and use `g_state.allocator.free(...)` in cleanup.

### 2.3 `seat_name` callback doesn't check `strdup` return

Location: `src/wl-client.c:484-485`.

```c
wlclient_input_device* input_device = find_input_device_by_seat(wl_seat);
input_device->seat_name = g_state.allocator.strdup(name);
```

`strdup` (POSIX) returns `NULL` on OOM and sets `errno = ENOMEM`. The code
doesn't check, so a later reader that assumes `seat_name != NULL` will NULL-
deref. The init validator doesn't require `seat_name` (see 2.1), so a NULL
here won't crash init, but any future log line or diagnostic that prints
`seat_name` will. This is a latent crash.

Fix: check the return, fall back to a sentinel ("unknown") or log and
continue without a name.

### 2.4 `seat_name` leaks previous name if the event is re-emitted

Location: `src/wl-client.c:485`.

```c
input_device->seat_name = g_state.allocator.strdup(name);
```

The wl_seat spec (`wayland.xml:1990-2010`) says:

> The name event is sent after binding to the seat global, and should be sent
> before announcing capabilities. This event only sent once per seat object,
> and the name does not change over the lifetime of the wl_seat global.

So in a protocol-conformant world this leak is dead code — but a buggy
compositor that resends the event (or a hot-plug corner case where the seat
is torn down and re-created with the same `wl_seat*` after a rebind that we
missed) would leak the prior `strdup` buffer. Trivial to fix defensively.

Fix: `if (input_device->seat_name) free(input_device->seat_name);` before the
assignment. Combine with 2.2 (use allocator-aware free).

### 2.5 `register_global` aborts the process on broken compositor input

Location: `src/wl-client.c:272, 275, 281, 287, 293, 300, 306, 318`.

Every failure in `register_global` goes through `WLCLIENT_PANIC`, which in
turn calls `abort()` (`include/debug.h:72-80`). Cases that abort:

- `wl_compositor` re-registered
- `wl_registry_bind` returning NULL for any interface
- `wl_*_add_listener` returning non-zero (listener already attached)

These are all conditions a malicious or buggy compositor can trigger.
`wlclient_init` is advertised as returning `WLCLIENT_ERROR_INIT_FAILED`, so
the contract is "init can fail gracefully" — but several failure modes
inside the registry callback break that contract by nuking the process.

Logic-wise this also means `wlclient_init` can abort mid-init without running
`wlclient_shutdown`, leaking the partially-built display/registry. The `error:`
label is unreachable from inside the callback.

Fix: in the callback, set a sticky error flag in `g_state` and return; have
`wlclient_init` check the flag after `wl_display_roundtrip` and branch to the
`error:` label. Reserve `WLCLIENT_PANIC` for honest invariant violations in
our own code.

### 2.6 `wl_subcompositor` hard-required but not actually used yet

Location: `src/wl-client.c:107`.

```c
ENSURE_OR_GOTO_ERR(g_state.subcompositor);
```

The refactor currently initializes state only — there are no surfaces, no
subsurfaces, no reason to demand `wl_subcompositor`. Making it mandatory
means `wlclient_init` fails on any compositor that doesn't expose it (rare
but real: some embedded compositors). The dependency becomes legitimate once
subsurface-based decorations actually exist.

Fix: demote to soft-optional until there's real use; or wrap the requirement
behind a feature flag that callers opt into.

---

## 3. Robustness & sequencing

### 3.1 No third roundtrip after `wl_seat_get_pointer` / `wl_seat_get_keyboard`

Location: `src/wl-client.c:111-115`.

```c
ret = wl_display_roundtrip(g_state.display);
ENSURE_OR_GOTO_ERR(ret >= 0);
ENSURE_OR_GOTO_ERR(g_state.preffered_pixel_format >= 0);
```

The sequence is:

1. Roundtrip 1 — processes registry globals. During this roundtrip the
   handler binds wl_compositor, wl_shm, wl_seat, etc. The seat bind queues
   a seat capabilities event and a seat name event from the server.
2. Roundtrip 2 — processes those queued events. `seat_capabilities` runs and
   calls `wl_seat_get_pointer` / `wl_seat_get_keyboard`, creating two new
   child proxies. The server will send initial events for them (most
   importantly `wl_keyboard.keymap`, and potentially `wl_keyboard.repeat_info`
   and `wl_keyboard.modifiers`).
3. Those initial events require **a third roundtrip** to be delivered before
   `wlclient_init` returns.

Right now the init doesn't validate keymap arrival, so this isn't a crashing
bug. It becomes one the moment any code in init wants to assert "keymap
loaded" or "initial modifier state known". When xkbcommon integration lands
(TODO in `keyboard_keymap`) this gap will bite.

Fix: add a final `wl_display_roundtrip` after the existing second roundtrip,
or deliberately defer keymap processing to the first main-loop iteration and
document it.

### 3.2 Shutdown order will break once surfaces exist

Location: `src/wl-client.c:145-150`.

xdg_wm_base `destroy` request (xdg-shell.xml:57-65):

> Destroying a bound xdg_wm_base object while there are surfaces still alive
> created by this xdg_wm_base object instance is illegal and will result in a
> defunct_surfaces error.

Protocol source:
<https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/xdg-shell/xdg-shell.xml#L57>
(local: `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml:57`)

Today the array of surfaces is empty so the order happens to work. Once
window creation lands, shutdown must destroy all xdg_toplevel / xdg_surface
instances before destroying `xdgWmBase`, otherwise the compositor raises
`defunct_surfaces` and kills the client. Worth wiring the teardown in
dependency order **now**, before forgetting.

Similarly, `wl_shm_pool` / `wl_buffer` objects must be destroyed before
`wl_shm`, and any `wl_subsurface` before `wl_subcompositor`.

### 3.3 `WL_SEAT_CAPABILITY_TOUCH` is silently unhandled

Location: `src/wl-client.c:397-466`.

`seat_capabilities` handles keyboard and pointer. Touch is silently dropped.
Combined with 2.1, a touch-only seat fails init even though the code could
in principle support it. Not a bug per se (touch isn't implemented yet), but
the validator in 2.1 elevates it to one.

TODO.md already notes multi-seat limitations — touch belongs in the same
conversation.

### 3.4 `shm_pick_format` ignores XRGB8888 which is also spec-guaranteed

Location: `src/wl-client.c:353-365`.

```c
if (format == WL_SHM_FORMAT_ARGB8888) {
    g_state.preffered_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
    ...
}
```

Per wl_shm.format enum (wayland.xml:305-316):

> wl_shm format `argb8888` and `xrgb8888` must always be supported.

Only ARGB is recognized. If for any reason the compositor delays advertising
ARGB (e.g. sends it after other formats and the roundtrip ends early), the
`preffered_pixel_format >= 0` check at `src/wl-client.c:115` fails and init
errors out — on a compositor that is technically spec-compliant.

Fix: accept the first known-good format seen (ARGB first, XRGB as fallback),
or initialize to ARGB unconditionally since the spec mandates it.

### 3.5 `store_new_input_device` doesn't record a failure path

Location: `src/wl-client.c:218-233`.

```c
WLCLIENT_PANIC(
    g_state.input_devices_count < WLCLIENT_MAX_INPUT_DEVICES,
    ...
);
```

If a compositor advertises more than `WLCLIENT_MAX_INPUT_DEVICES = 5` seats,
init aborts the process. Five seats is high but not infinite (multi-seat
demo workstations, kiosk rigs, Weston multi-seat tests). Same category as
2.5: this should be a graceful error, not `abort()`.

---

## 4. Minor / cosmetic

### 4.1 Typo: `listender`

`src/wl-client.c:289` — `static const struct xdg_wm_base_listener listender`.
Functional (variable name is local), but embarrassing in grep.

### 4.2 Misleading comment in `destroy_input_device`

`src/wl-client.c:214`:

```c
// Marks the pointer as unused along with zeroing out everything else:
memset(input_device, 0, sizeof(*input_device));
```

"Marks the pointer as unused" — the pointer child object was already released
on line 209. The `memset` clears the `used` flag on the device (and
everything else). Read as-is the comment implies the memset is what releases
the pointer, which is wrong.

### 4.3 `free(NULL)` guarding

`src/wl-client.c:211` — `if (input_device->seat_name) free(...)`. `free(NULL)`
is a POSIX no-op, so the guard is redundant. Fix or leave; not a bug.

### 4.4 `register_global_remove` is a stub

`src/wl-client.c:335-339` logs the id and does nothing. If any bound global
disappears at runtime (hot-unplug of wl_seat, wl_output going away), the
proxy keeps pointing at a dead server-side resource and subsequent requests
may trip protocol errors. Not critical at the current stage of the refactor;
will need real handling before shipping.

---

## Summary

| #   | Category                | Severity |
|-----|-------------------------|----------|
| 1.1 | wl_seat destroy vs release | protocol |
| 1.2 | wl_shm destroy vs release  | protocol |
| 1.3 | wl_pointer/wl_keyboard release without version guard | protocol (latent) |
| 1.4 | No flush before disconnect | protocol |
| 2.1 | Init demands pointer + keyboard on every seat | logic |
| 2.2 | `free` used on allocator-`strdup` memory | logic / design gap |
| 2.3 | `strdup` return not checked | robustness |
| 2.4 | `seat_name` leaks on re-emission | robustness |
| 2.5 | `WLCLIENT_PANIC` in registry callback breaks init contract | design |
| 2.6 | `wl_subcompositor` required but unused | design |
| 3.1 | Missing 3rd roundtrip for keymap etc. | sequencing |
| 3.2 | Shutdown order will break with surfaces | latent |
| 3.3 | Touch capability silently ignored | feature gap interacts w/ 2.1 |
| 3.4 | XRGB8888 fallback missing | robustness |
| 3.5 | `store_new_input_device` aborts on >5 seats | robustness |
| 4.1 | `listender` typo | cosmetic |
| 4.2 | Misleading comment in destroy | cosmetic |
| 4.3 | Redundant NULL check around `free` | cosmetic |
| 4.4 | `register_global_remove` unimplemented | future work |

Hardest-to-notice bugs worth fixing first: **1.4** (flush on shutdown) and
**2.5** (registry-callback panics). Everything else is either obvious once
pointed at or cosmetic.
