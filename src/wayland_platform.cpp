#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "platform.h"
#include "wayland_platform.h"
#include "logger_config.h"

namespace {

bool g_useSoftwareRenderer = false;

wl_display *g_display = nullptr;
wl_registry *g_registry = nullptr;
wl_registry_listener g_registerListener = {};

xdg_wm_base* g_xdgWmBase = nullptr;
xdg_wm_base_listener g_xdgWmBaseListener = {};

UserInputEvents g_userInputEventHandlers = {};

wl_compositor *g_compositor = nullptr;
wl_surface* g_surface = nullptr;
xdg_surface* g_xdgSurface = nullptr;
xdg_toplevel* g_xdgToplevel = nullptr;
xdg_surface_listener g_xdgSurfaceListener = {};

i32 g_windowWidth = 0;
i32 g_windowHeight = 0;
i32 g_windowStride = 0;

// ############################################ BEGIN Software Rendering State #########################################

[[maybe_unused]] wl_shm* g_shm = nullptr;
[[maybe_unused]] wl_shm_listener g_shmListener = {};
[[maybe_unused]] wl_buffer* g_buffer = nullptr;
[[maybe_unused]] wl_buffer_listener g_bufferListener = {};
[[maybe_unused]] u8* g_mappedData = nullptr;
[[maybe_unused]] bool g_bufferIsReady = false;
[[maybe_unused]] PixelFormat g_pixelFormat = PixelFormat::UNDEFINED;

// ############################################ END Software Rendering State ###########################################

wl_seat *g_seat = nullptr;
wl_seat_listener g_seatListener = {};
const char* g_seatName = nullptr;

// ############################################ BEGIN Pointer State ####################################################

wl_pointer *g_pointer = nullptr;
wl_pointer_listener g_pointerListener = {};

// Latest pointer position in surface-local coordinates (integer pixels).
i32 g_pointerX = 0;
i32 g_pointerY = 0;

// Pending scroll data collected until the matching pointerFrame flushes it.
struct WaylandScrollState {
    bool hasAxis = false;
    wl_fixed_t value = 0; // Continuous delta.
    i32 value120 = 0; // High-res discrete delta (multiples of 120).
    bool hasValue120 = false;
};
WaylandScrollState g_scrollState[2]; // Up and down. TODO2: Horizontal scrolling can be added here.
bool g_scrollFrameHasData = false;

// ############################################ EBD Pointer State ######################################################
// ############################################ BEGIN Keyboard State ###################################################

wl_keyboard *g_keyboard = nullptr;
wl_keyboard_listener g_keyboardListener = {};
KeyboardModifiers g_keyboardModifiers = KeyboardModifiers::MODNONE;

// ############################################ END Keyboard State #####################################################

inline void handleDisplayFlushError(i32 errCode, const char* context);

void seatCapabilities(void*, wl_seat *seat, u32 capabilities);
void seatName(void*, struct wl_seat * seat, const char *name);

void registerGlobal(void*, wl_registry* registry, u32 name, const char* interface, u32 version);
void registerGlobalRemove(void*, wl_registry*, u32 name);

void releaseBuffer(void *data, struct wl_buffer *);

void xdgWmBasePing(void*, xdg_wm_base *xdgWmBase, u32 serial);

void xdgSurfaceConfigure(void*, xdg_surface *xdgSurface, u32 serial);

void pointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
void pointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface);
void pointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t sx, wl_fixed_t sy);
void pointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time, u32 button, u32 state);
void pointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value);
void pointerFrame(void* data, wl_pointer* pointer);
void pointerAxisSource(void* data, wl_pointer* pointer, u32 axis_source);
void pointerAxisStop(void* data, wl_pointer* pointer, u32 time, u32 axis);
void pointerAxisDiscrete(void* data, wl_pointer* pointer, u32 axis, i32 discrete);
void pointerAxisValue120(void* data, wl_pointer* pointer, u32 axis, i32 value120);
void pointerAxisRelativeDirection(void* data, wl_pointer* pointer, u32 axis, u32 direction);

void keyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size);
void keyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface, wl_array* keys);
void keyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface);
void keyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state);
void keyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group);
void keyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay);

} // namespace

void platformInit() {
    Panic(core::loggerSetTag(i32(LoggerTags::T_PLATFORM), "WAYLAND_LINUX_PLATFORM"_sv));
    core::loggerSetLevel(core::LogLevel::L_INFO, LoggerTags::T_PLATFORM);
    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_PLATFORM, "Platform");

    i32 ret;

    g_display = wl_display_connect(NULL);
    Panic(g_display, "Failed to connect to Wayland display");
    logInfoTagged(LoggerTags::T_PLATFORM, "Display connection established.");

    // Setup the registry
    {
        g_registry = wl_display_get_registry(g_display);
        Panic(g_registry, "Failed to get registry for Wayland display");

        g_registerListener.global = registerGlobal;
        g_registerListener.global_remove = registerGlobalRemove;
        ret = wl_registry_add_listener(g_registry, &g_registerListener, nullptr);
        PanicFmt(ret == 0, "wl_registry_add_listener exited with {}", ret);

        ret = wl_display_roundtrip(g_display);  // Block until all pending request are processed
        PanicFmt(ret >= 0, "wl_display_roundtrip exited with {}", ret);

        logInfoTagged(LoggerTags::T_PLATFORM, "Registry initialized.");
    }

    Panic(g_compositor, "Compositor did not advertise wl_compositor");
    Panic(g_xdgWmBase, "Compositor did not advertise xdg_wm_base");
    Panic(g_shm, "Compositor did not advertise wl_shm");
    Panic(g_seat, "Compositor did not advertise wl_seat");
    Panic(g_pixelFormat == PixelFormat::UNDEFINED, "Did not find supported pixel format");
}

void platformShutdown() {
    LOG_INFO_BLOCK_SHUTDOWN(LoggerTags::T_PLATFORM, "Platform");

    if (g_keyboard) {
        wl_keyboard_destroy(g_keyboard);
        g_keyboard = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Keyboard destroyed.");
    }

    if (g_pointer) {
        wl_pointer_release(g_pointer);
        g_pointer = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Mouse destroyed.");
    }

    if (g_seat) {
        wl_seat_destroy(g_seat);
        g_seat = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Seat destroyed.");
    }

    if (g_compositor) {
        wl_compositor_destroy(g_compositor);
        g_compositor = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Compositor destroyed.");
    }

    if (g_xdgToplevel) {
        xdg_toplevel_destroy(g_xdgToplevel);
        g_xdgToplevel = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "XDG TopLevel destroyed.");
    }

    if (g_xdgSurface) {
        xdg_surface_destroy(g_xdgSurface);
        g_xdgSurface = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "XDG Surface destroyed.");
    }

    if (g_surface) {
        wl_surface_destroy(g_surface);
        g_surface = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Surface destroyed.");
    }

    if (g_xdgWmBase) {
        xdg_wm_base_destroy(g_xdgWmBase);
        g_xdgWmBase = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "XDG WM Base destroyed.");
    }

    if (g_buffer) {
        wl_buffer_destroy(g_buffer);
        g_buffer = nullptr;
        g_bufferIsReady = false;
        logInfoTagged(LoggerTags::T_PLATFORM, "Buffer destroyed.");
    }

    if (g_mappedData) {
        munmap(g_mappedData, size_t(g_windowStride * g_windowHeight));
        g_mappedData = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Frame buffer memory unmapped.");
    }

    if (g_shm) {
        wl_shm_destroy(g_shm);
        g_shm = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "SHM destroyed.");
    }

    if (g_registry) {
        wl_registry_destroy(g_registry);
        g_registry = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Registry destroyed.");
    }

    if (g_display) {
        wl_display_disconnect(g_display);
        g_display = nullptr;
        logInfoTagged(LoggerTags::T_PLATFORM, "Display disconnected.");
    }
}

void platformOpenOSWindow(const OpenWindowInfo& openInfo) {
    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_PLATFORM, "Open OS Window");

    i32 ret;

    const char* windowName = openInfo.name;
    g_windowWidth = openInfo.width;
    g_windowHeight = openInfo.height;
    g_windowStride = g_windowWidth * 4;
    g_useSoftwareRenderer = openInfo.useSoftwareRendering;
    g_userInputEventHandlers = openInfo.userInputEvents;

    // Create the surface
    g_surface = wl_compositor_create_surface(g_compositor);
    Panic(g_surface, "wl_compositor_create_surface failed to create surface");
    g_xdgSurface = xdg_wm_base_get_xdg_surface(g_xdgWmBase, g_surface);
    Panic(g_xdgSurface, "xdg_wm_base_get_xdg_surface failed to create surface");
    g_xdgToplevel = xdg_surface_get_toplevel(g_xdgSurface);
    Panic(g_xdgToplevel, "xdg_surface_get_toplevel failed to create surface");

    xdg_toplevel_set_title(g_xdgToplevel, windowName);

    g_xdgSurfaceListener.configure = xdgSurfaceConfigure;
    ret = xdg_surface_add_listener(g_xdgSurface, &g_xdgSurfaceListener, nullptr);
    PanicFmt(ret == 0, "xdg_surface_add_listener exited with {}", ret);

    wl_surface_commit(g_surface);
    ret = wl_display_roundtrip(g_display);
    PanicFmt(ret >= 0, "wl_display_roundtrip exited with {}", ret);
    logInfoTagged(LoggerTags::T_PLATFORM, "Surface created");
}

void platformPollEvents() {
    // Drain existing events
    while (wl_display_prepare_read(g_display) != 0) {
        [[maybe_unused]] i32 ret = wl_display_dispatch_pending(g_display);
        AssertFmt(ret >= 0, "wl_display_dispatch_pending exited with {}", ret);
    }

    // Poll the Wayland socket without waiting
    struct pollfd pfd = { wl_display_get_fd(g_display), POLLIN, 0 };
    i32 pollres = poll(&pfd, 1, 0); // timeout = 0 → never block

    if (pollres > 0) {
        [[maybe_unused]] i32 ret = wl_display_read_events(g_display);
        AssertFmt(ret >= 0, "wl_display_read_events exited with {}", ret);
    }
    else {
        wl_display_cancel_read(g_display);
    }

    {
        [[maybe_unused]] i32 ret = wl_display_dispatch_pending(g_display);
        AssertFmt(ret >= 0, "wl_display_dispatch_pending exited with {}", ret);
    }

    {
        i32 ret = wl_display_flush(g_display);
        handleDisplayFlushError(ret, "event loop");
    }
}

void platformCreateSoftRendCtx(SoftwareRenderingContext& out) {
    // Sanity check:
    Assert(g_buffer && g_display && g_surface && g_windowWidth && g_windowHeight && g_mappedData);
    Assert(g_useSoftwareRenderer, "useSoftwareRenderer must be true on platformOpenOSWindow");

    out.bufferIsReady = &g_bufferIsReady;
    out.display = g_display;
    out.surface = g_surface;

    FrameBuffer fb1 = {
        .width = g_windowWidth,
        .height = g_windowHeight,
        .pixelFormat = g_pixelFormat,
        .wlBuffer = g_buffer,
        .data = g_mappedData,
    };

    out.frameBuffers.push(fb1);
}

namespace {

inline void handleDisplayFlushError(i32 errCode, const char* context) {
    if (errCode < 0) {
        if (errno == EAGAIN) {
            logDebugTagged(LoggerTags::T_PLATFORM, "wl_display_flush would block (EAGAIN) during {}", context);
        }
        else {
            AssertFmt(false, "wl_display_flush failed during {} with errno={}", context, errno);
        }
    }
}

void xdgWmBasePing(void*, xdg_wm_base *xdgWmBase, u32 serial) {
    logDebugTagged(LoggerTags::T_PLATFORM, "Ping received serial: {}", serial);
    xdg_wm_base_pong(xdgWmBase, serial);
}

void seatCapabilities(void*, wl_seat *seat, u32 capabilities) {
    if (seat == g_seat) {
        logInfo("Capabilities: {}", capabilities);

        bool supportsKeyboard = capabilities & wl_seat_capability::WL_SEAT_CAPABILITY_KEYBOARD;
        bool supportsMouse = capabilities & wl_seat_capability::WL_SEAT_CAPABILITY_POINTER;

        if (!g_pointer && supportsMouse) {
            g_pointer = wl_seat_get_pointer(g_seat);
            Panic(g_pointer, "failed to get pointer from seat.");

            g_pointerListener.enter = pointerEnter;
            g_pointerListener.leave = pointerLeave;
            g_pointerListener.motion = pointerMotion;
            g_pointerListener.button = pointerButton;
            g_pointerListener.axis = pointerAxis;
            g_pointerListener.frame = pointerFrame;
            g_pointerListener.axis_source = pointerAxisSource;
            g_pointerListener.axis_stop = pointerAxisStop;
            g_pointerListener.axis_discrete = pointerAxisDiscrete;
            g_pointerListener.axis_value120 = pointerAxisValue120;
            g_pointerListener.axis_relative_direction = pointerAxisRelativeDirection;

            wl_pointer_add_listener(g_pointer, &g_pointerListener, nullptr);

            logInfo("Registered mouse successfully");
        }
        else if (g_pointer) {
            wl_pointer_release(g_pointer);
            g_pointer = nullptr;
            logInfo("Pointer capability dropped; pointer released.");
        }

        if (!g_keyboard && supportsKeyboard) {
            g_keyboard = wl_seat_get_keyboard(g_seat);
            Panic(g_keyboard, "failed to get pointer from seat.");

            g_keyboardListener.keymap = keyboardKeymap;
            g_keyboardListener.enter = keyboardEnter;
            g_keyboardListener.leave = keyboardLeave;
            g_keyboardListener.key = keyboardKey;
            g_keyboardListener.modifiers = keyboardModifiers;
            g_keyboardListener.repeat_info = keyboardRepeatInfo;
            wl_keyboard_add_listener(g_keyboard, &g_keyboardListener, nullptr);

            logInfo("Registered keyboard successfully");
        }
        else if (g_keyboard) {
            auto ptr = g_keyboard;
            if (g_keyboard) g_keyboard = nullptr;
            wl_keyboard_release(ptr);
        }
    }
}

void seatName(void*, struct wl_seat * seat, const char *name) {
    if (seat == g_seat) {
        g_seatName = name;
        logInfo("Seat name: {}", g_seatName);
    }
}

void registerGlobal(void*, wl_registry* registry, u32 name, const char* interface, u32 version) {
    auto pickVersion = [&version](auto& x) {
        i32 compositorSupported = x.version; // if this can be negative I will stop using wayland.
        u32 bindVersion = core::core_min(version, u32(compositorSupported));
        return bindVersion;
    };

    auto pickFormatCb = [](void*, wl_shm*, u32 format) {
        if (format == WL_SHM_FORMAT_ARGB8888) {
            g_pixelFormat = PixelFormat::ARGB8888;
        }
    };

    if (core::sv(interface).eq("wl_seat")) {
        g_seat = reinterpret_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, pickVersion(wl_seat_interface))
        );
        Panic(g_seat, "Failed to bind localSeat");

        g_seatListener.capabilities = seatCapabilities;
        g_seatListener.name = seatName;
        wl_seat_add_listener(g_seat, &g_seatListener, nullptr);
    }
    else if (core::sv(interface).eq("xdg_wm_base")) {
        g_xdgWmBase = reinterpret_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, pickVersion(xdg_wm_base_interface))
        );
        Panic(g_xdgWmBase, "Failed to bind g_xdgWmBase");

        // Setup ping handler
        g_xdgWmBaseListener.ping = xdgWmBasePing;
        i32 ret = xdg_wm_base_add_listener(g_xdgWmBase, &g_xdgWmBaseListener, nullptr);
        PanicFmt(ret == 0, "xdg_wm_base_add_listener exited with {}", ret);
    }
    else if (core::sv(interface).eq("wl_compositor")) {
        g_compositor = reinterpret_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, pickVersion(wl_compositor_interface))
        );
        Panic(g_compositor, "Failed to bind g_compositor");
    }
    else if (core::sv(interface).eq("wl_shm")) {
        g_shm = reinterpret_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, pickVersion(wl_shm_interface))
        );
        Panic(g_shm, "Failed to bind g_shm");

        g_shmListener.format = pickFormatCb;
        i32 ret = wl_shm_add_listener(g_shm, &g_shmListener, nullptr);
        PanicFmt(ret == 0, "wl_shm_add_listener exited with {}", ret);
    }
    else {
        // Interfaces that are not used by the application:
        logDebugTagged(LoggerTags::T_PLATFORM, "Register Global unhandled interfaces: {}", interface);
        return;
    }

    logInfoTagged(LoggerTags::T_PLATFORM, "Bound interface: {}", interface);
}

void registerGlobalRemove(void*, wl_registry*, u32 name) {
    logWarnTagged(LoggerTags::T_PLATFORM, "Register Global Remove called for objId: {}", name);
    // TODO2: I might want to handle this just in case.
}

void releaseBuffer(void *data, struct wl_buffer *) {
    logTraceTagged(LoggerTags::T_PLATFORM, "Buffer Released.");
    bool* ready = static_cast<bool*>(data);
    *ready = true;
}

void xdgSurfaceConfigure(void*, xdg_surface *xdgSurface, u32 serial) {
    logInfoTagged(LoggerTags::T_PLATFORM, "Xdg Surface Configure Event serial: {}", serial);

    xdg_surface_ack_configure(xdgSurface, serial);

    if (g_useSoftwareRenderer) {
        // TODO: This code needs to be a function and it needs to recreate the buffer
        //       every time that the window is resized, so this is temporary code !
        //       It also needs to use double/triple buffering.

        i32 ret;

        i32 size = g_windowStride * g_windowHeight;

        i32 fd = memfd_create("tmp", 0);
        defer { close(fd); };
        PanicFmt(fd >= 0, "Failed to create an anonymous file to store the backing pixels buffer, err_code={}", errno);
        ret = ftruncate(fd, addr_off(size));
        PanicFmt(ret == 0, "Failed to truncate the anonymous file to size={}, err_code={}", size, errno);

        // Map it in the virtual address space.
        g_mappedData = reinterpret_cast<u8*>(
            mmap(nullptr, addr_size(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
        );
        PanicFmt(g_mappedData != MAP_FAILED, "Failed to memory map the anonymous file, err_code={}", errno);

        // TODO: might want to avoid doing this in release builds:
        core::memset(g_mappedData, u8(0x00), addr_size(size));

        wl_shm_pool* pool = wl_shm_create_pool(g_shm, fd, i32(size));
        Panic(pool, "wl_shm_create_pool failed to create pool.");
        defer { wl_shm_pool_destroy(pool); };
        g_buffer = wl_shm_pool_create_buffer(pool, 0,
            g_windowWidth, g_windowHeight, g_windowStride,
            WL_SHM_FORMAT_ARGB8888);
        Panic(g_buffer, "wl_shm_pool_create_buffer failed to create buffer.");

        // First time add the release buffer listener to the render state:
        g_bufferListener.release = releaseBuffer;
        g_bufferIsReady = true;
        ret = wl_buffer_add_listener(g_buffer, &g_bufferListener, &g_bufferIsReady);
        PanicFmt(ret == 0, "wl_buffer_add_listener exited with {}", ret);

        // Hint to the compositor that the window will be non-transperant:
        wl_region* emptyRegion = wl_compositor_create_region(g_compositor);
        Panic(emptyRegion, "wl_compositor_create_region failed");
        wl_region_add(emptyRegion, 0, 0, g_windowWidth, g_windowHeight);
        wl_surface_set_opaque_region(g_surface, emptyRegion);
        wl_region_destroy(emptyRegion);

        wl_surface_attach(g_surface, g_buffer, 0, 0);
        wl_surface_damage_buffer(g_surface, 0, 0, g_windowWidth, g_windowHeight);
        wl_surface_commit(g_surface);
        ret = wl_display_flush(g_display);
        handleDisplayFlushError(ret, "buffer commit");
    }
}

void pointerEnter(void*, [[maybe_unused]] wl_pointer* pointer, u32, wl_surface*, wl_fixed_t, wl_fixed_t) {
    // Pointer focus entered our surface; Wayland expects clients to set/update the cursor image here.
    Assert(pointer == g_pointer, "Unexpected pointer object on enter");

    if (!g_userInputEventHandlers.mouseEnterOrLeaveCallback) return;

    g_userInputEventHandlers.mouseEnterOrLeaveCallback(true);
}

void pointerLeave(void*, [[maybe_unused]] wl_pointer* pointer, u32, wl_surface*) {
    // Pointer focus left our surface; Wayland expects clients to drop the cursor image here.
    Assert(pointer == g_pointer, "Unexpected pointer object on leave");

    if (!g_userInputEventHandlers.mouseEnterOrLeaveCallback) return;

    g_userInputEventHandlers.mouseEnterOrLeaveCallback(false);
}

void pointerMotion(void*, [[maybe_unused]] wl_pointer* pointer, u32, wl_fixed_t sx, wl_fixed_t sy) {
    Assert(pointer == g_pointer, "Unexpected pointer object on motion");

    // I want to set these for pointer button events even when mouseMoveCallback is not set.
    g_pointerX = wl_fixed_to_int(sx);
    g_pointerY = wl_fixed_to_int(sy);

    if (!g_userInputEventHandlers.mouseMoveCallback) return;

    g_userInputEventHandlers.mouseMoveCallback(g_pointerX, g_pointerY);
}

void pointerButton(void*, [[maybe_unused]] wl_pointer* pointer, u32, u32, u32 button, u32 state) {
    Assert(pointer == g_pointer, "Unexpected pointer object on button");

    if (!g_userInputEventHandlers.mouseClickCallback) return;

    bool isPress = (state == WL_POINTER_BUTTON_STATE_PRESSED);

    MouseButton mapped = MouseButton::NONE;
    switch (button) {
        case BTN_LEFT: mapped = MouseButton::LEFT; break;
        case BTN_MIDDLE: mapped = MouseButton::MIDDLE; break;
        case BTN_RIGHT: mapped = MouseButton::RIGHT; break;

        default:
            logWarnTagged(LoggerTags::T_PLATFORM, "unsupported mouse button type {}", button);
            return;
    }

    // Position captured from latest motion event.
    i32 x = g_pointerX;
    i32 y = g_pointerY;
    KeyboardModifiers mods = KeyboardModifiers::MODNONE; // TODO: add support for mods when keyboard events are handled.

    g_userInputEventHandlers.mouseClickCallback(isPress, mapped, x, y, mods);
}

void pointerAxis(void*, [[maybe_unused]] wl_pointer* pointer, u32 /*time*/, u32 axis, wl_fixed_t value) {
    // Continuous scroll/axis delta for vertical or horizontal scrolling; delivered once per axis before frame().

    Assert(pointer == g_pointer, "Unexpected pointer object on axis");

    if (!g_userInputEventHandlers.mouseScrollCallback) return;
    if (axis >= 2) return;

    g_scrollFrameHasData = true;
    g_scrollState[axis].hasAxis = true;
    g_scrollState[axis].value += value;
}

void pointerAxisDiscrete(void*, [[maybe_unused]] wl_pointer* pointer, u32 axis, i32 discrete) {
    // Deprecated discrete scroll steps; integer wheel notches paired with the axis() event in the same frame.

    Assert(pointer == g_pointer, "Unexpected pointer object on axisDiscrete");

    if (!g_userInputEventHandlers.mouseScrollCallback) return;
    if (axis >= 2) return;

    g_scrollFrameHasData = true;
    g_scrollState[axis].hasAxis = true;
    g_scrollState[axis].value += wl_fixed_from_int(discrete);
}

void pointerAxisValue120(void*, [[maybe_unused]] wl_pointer* pointer, u32 axis, i32 value120) {
    // High-resolution discrete scrolling: multiples of 120 represent a full wheel detent (since wl_pointer v8).

    Assert(pointer == g_pointer, "Unexpected pointer object on AxisValue120");

    if (!g_userInputEventHandlers.mouseScrollCallback) return;
    if (axis >= 2) return;

    g_scrollFrameHasData = true;
    g_scrollState[axis].hasAxis = true;
    g_scrollState[axis].value120 += value120;
    g_scrollState[axis].hasValue120 = true;
}

void pointerAxisSource(void*, wl_pointer*, u32) {
    // Describes what produced the axis events in this frame (e.g. wheel, finger, continuous touchpad scroll).
}

void pointerAxisStop(void*, wl_pointer*, u32, u32) {
    // Signals the end of a scrolling sequence for a given axis (mainly sent for touchpad finger scrolling).
}

void pointerAxisRelativeDirection(void*, wl_pointer*, u32 /*axis*/, u32 /*direction*/) {
    // Indicates whether the physical scroll direction matches or is inverted relative to the axis value (natural scroll).
}

void pointerFrame(void*, [[maybe_unused]] wl_pointer* pointer) {
    // Marks the end of a batch of pointer events (motion/button/axis); accumulate data until this fires.
    Assert(pointer == g_pointer, "Unexpected pointer object on frame");

    if (!g_scrollFrameHasData || !g_userInputEventHandlers.mouseScrollCallback) return;

    defer {
        // Clear frame state.
        g_scrollFrameHasData = false;
        core::memset(g_scrollState, {}, CORE_C_ARRLEN(g_scrollState));
    };

    auto flushAxisState = [](u32 axis, MouseScrollDirection& dir) -> bool {
        const auto& scrollState = g_scrollState[axis];

        if (!scrollState.hasAxis) return false;
        if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            // TODO2: Horizontal scroll isn't supported.
            return false;
        }

        if (scrollState.hasValue120) {
            // TODO2: This ignores scroll magnitude/velocity, so on some devices (hi‑res wheels/touchpads) user experience
            //        might be unsatisfactory.
            f64 real = wl_fixed_to_double(scrollState.value120);
            if (real > 0) dir = MouseScrollDirection::DOWN;
            else if (real < 0) dir = MouseScrollDirection::UP;
            else dir = MouseScrollDirection::NONE;
        }
        else {
            i32 discrete = wl_fixed_to_int(scrollState.value);
            if (discrete > 0) dir = MouseScrollDirection::DOWN;
            else if (discrete < 0) dir = MouseScrollDirection::UP;
            else dir = MouseScrollDirection::NONE;
        }

        return true;
    };

    MouseScrollDirection dir;
    if (flushAxisState(WL_POINTER_AXIS_VERTICAL_SCROLL, dir)) {
        g_userInputEventHandlers.mouseScrollCallback(dir, g_pointerX, g_pointerY);
    }
}

void keyboardKeymap(void*, [[maybe_unused]] wl_keyboard* keyboard, u32 format, i32 fd, u32 size) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on keymap");
    logDebugTagged(LoggerTags::T_PLATFORM, "keymap format={}, fd={}, size={}", format, fd, size);

    // We do not consume the keymap; close the fd to avoid leaking it.
    if (fd >= 0) close(fd);
}

void keyboardEnter(void*, [[maybe_unused]] wl_keyboard* keyboard, u32 serial, wl_surface*, wl_array* keys) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on enter");
    size_t keyBytes = keys ? keys->size : 0;
    logDebugTagged(LoggerTags::T_PLATFORM, "keyboard enter serial={}, key_bytes={}", serial, keyBytes);
}

void keyboardLeave(void*, [[maybe_unused]] wl_keyboard* keyboard, u32 serial, wl_surface*) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on leave");
    logDebugTagged(LoggerTags::T_PLATFORM, "keyboard leave serial={}", serial);
    g_keyboardModifiers = KeyboardModifiers::MODNONE;
}

inline void updateModifiersFromKey(u32 key, bool isPress) {
    auto setBit = [isPress](KeyboardModifiers bit) {
        if (isPress) {
            g_keyboardModifiers = g_keyboardModifiers | bit;
        }
        else {
            g_keyboardModifiers = g_keyboardModifiers & (~bit);
        }
    };

    switch (key) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
            setBit(KeyboardModifiers::MODSHIFT);
            break;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
            setBit(KeyboardModifiers::MODCONTROL);
            break;
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
            setBit(KeyboardModifiers::MODALT);
            break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            setBit(KeyboardModifiers::MODSUPER);
            break;
        default:
            break;
    }
}

void keyboardKey(void*, [[maybe_unused]] wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on key");
    logDebugTagged(LoggerTags::T_PLATFORM, "key serial={}, time={}, key={}, state={}", serial, time, key, state);

    bool isPress = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    updateModifiersFromKey(key, isPress);

    if (!g_userInputEventHandlers.keyCallback) return;

    u32 vkcode = key;
    u32 scancode = key;
    g_userInputEventHandlers.keyCallback(isPress, vkcode, scancode, g_keyboardModifiers);
}

void keyboardModifiers(void*, [[maybe_unused]] wl_keyboard* keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on modifiers");
    logDebugTagged(LoggerTags::T_PLATFORM, "modifiers serial={}, dep={}, lat={}, lock={}, group={}", serial, mods_depressed, mods_latched, mods_locked, group);
}

void keyboardRepeatInfo(void*, [[maybe_unused]] wl_keyboard* keyboard, i32 rate, i32 delay) {
    Assert(keyboard == g_keyboard, "Unexpected keyboard on repeat info");
    logDebugTagged(LoggerTags::T_PLATFORM, "repeat rate={}, delay={}ms", rate, delay);
}

} // namespace
