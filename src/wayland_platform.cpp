#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>

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
wl_seat *g_seat = nullptr;
wl_registry_listener g_registerListener = {};

xdg_wm_base* g_xdgWmBase = nullptr;
xdg_wm_base_listener g_xdgWmBaseListener = {};

wl_compositor *g_compositor = nullptr;
wl_surface* g_surface = nullptr;
xdg_surface* g_xdgSurface = nullptr;
xdg_toplevel* g_xdgToplevel = nullptr;
xdg_surface_listener g_xdgSurfaceListener = {};

[[maybe_unused]] wl_shm* g_shm = nullptr;
[[maybe_unused]] wl_shm_listener g_shmListener = {};
[[maybe_unused]] wl_buffer* g_buffer = nullptr;
[[maybe_unused]] wl_buffer_listener g_bufferListener = {};
[[maybe_unused]] u8* g_mappedData = nullptr;
[[maybe_unused]] bool g_bufferIsReady = false;
[[maybe_unused]] PixelFormat g_pixelFormat = PixelFormat::UNDEFINED;

i32 g_windowWidth = 0;
i32 g_windowHeight = 0;
i32 g_windowStride = 0;

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

void xdgWmBasePing(void*, xdg_wm_base *xdgWmBase, uint32_t serial) {
    logInfoTagged(LoggerTags::T_PLATFORM, "Ping received serial: {}", serial);
    xdg_wm_base_pong(xdgWmBase, serial);
}

void registerGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto pickVersion = [&version](auto& x) {
        i32 compositorSupported = x.version; // if this can be negative I will stop using wayland.
        u32 bindVersion = core::core_min(version, u32(compositorSupported));
        return bindVersion;
    };

    auto pickFormatCb = [](void*, wl_shm*, uint32_t format) {
        if (format == WL_SHM_FORMAT_ARGB8888) {
            g_pixelFormat = PixelFormat::ARGB8888;
        }
    };

    if (core::sv(interface).eq("wl_seat")) {
        g_seat = reinterpret_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, pickVersion(wl_seat_interface))
        );
        Panic(g_seat, "Failed to bind localSeat");
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

void registerGlobalRemove(void*, wl_registry*, uint32_t name) {
    logWarnTagged(LoggerTags::T_PLATFORM, "Register Global Remove called for objId: {}", name);
    // TODO2: I might want to handle this just in case.
}

void releaseBuffer(void *data, struct wl_buffer *) {
    logTraceTagged(LoggerTags::T_PLATFORM, "Buffer Released.");
    bool* ready = static_cast<bool*>(data);
    *ready = true;
}

void xdgSurfaceConfigure(void*, xdg_surface *xdgSurface, uint32_t serial) {
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
