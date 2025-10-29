#include "core_init.h"

#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <sys/mman.h>
#include <poll.h>

#include "xdg-shell-client-protocol.h"

wl_display *g_display = nullptr;
wl_registry *g_registry = nullptr;
wl_seat *g_seat = nullptr;
wl_registry_listener g_registerListener = {};

wl_pointer *g_pointer = nullptr;
wl_pointer_listener g_mouseListener = {};

wl_buffer* g_buffer = nullptr;

wl_compositor *g_compositor = nullptr;

wl_surface* g_surface = nullptr;

wl_shm* g_shm = nullptr;
wl_shm_pool* g_pool = nullptr;

xdg_wm_base* g_xdgWmBase = nullptr;
xdg_surface* g_xdgSurface = nullptr;
xdg_toplevel* g_xdgToplevel = nullptr;
xdg_wm_base_listener g_xdgWmBaseListener = {};
xdg_surface_listener g_xdgSurfaceListener = {};

void registerGlobal(void* userData, wl_registry* registry,
                    uint32_t name, const char* interface, uint32_t version) {
    if (core::sv(interface).eq("wl_seat")) {
        // NOTE: this is the same as the global seat, but accessing it through the data argument is the correct pattern:
        wl_seat** seat = reinterpret_cast<wl_seat**>(userData);
        *seat = reinterpret_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, version)
        );
    } else if (core::sv(interface).eq("xdg_wm_base")) {
        g_xdgWmBase = reinterpret_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, version)
        );
    } else if (core::sv(interface).eq("wl_compositor")) {
        g_compositor = reinterpret_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, version)
        );
    } else if (core::sv(interface).eq("wl_shm")) {
        g_shm = reinterpret_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, version)
        );
    }
}

void registerGlobalRemove(void*, wl_registry*, uint32_t) {}

void xdgWmBasePing(void*, xdg_wm_base *wm, uint32_t serial) {
    xdg_wm_base_pong(wm, serial);
}

i32 width = 800, height = 900;
i32 stride = width * 4;
size_t size = size_t(stride) * size_t(height);
u8* g_data = nullptr;
i32 g_fd = -1;

void handleConfigure(void*, xdg_surface *surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);

    g_fd = memfd_create("tmp", 0);
    ftruncate(g_fd, addr_off(size));
    g_data = reinterpret_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0));

    for (size_t i = 0; i < size; i+=4) {
        g_data[i] = 0; // blue
        g_data[i+1] = 0; // green
        g_data[i+2] = 0; // red
        g_data[i+3] = 255; // alpha
    }

    g_pool = wl_shm_create_pool(g_shm, g_fd, i32(size));
    g_buffer = wl_shm_pool_create_buffer(g_pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(g_pool);

    wl_surface_attach(g_surface, g_buffer, 0, 0);
    wl_surface_damage_buffer(g_surface, 0, 0, width, height);
    wl_surface_commit(g_surface);
}

void mouseEnter(void*, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {}

void mouseLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}

void mouseMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {}

void mouseButton(void *, struct wl_pointer *, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    std::cout << "Button " << button << " state " << state << "\n";
}

void mouseAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}

void mouseAxisSource(void*, wl_pointer*, uint32_t) {}

void mouseAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}

void mouseAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

void mouseFrame(void*, wl_pointer*) {}

i32 main() {
    coreInit();

    // Setup the display
    {
        g_display = wl_display_connect(NULL);
        Panic(g_display, "Failed to connect to Wayland display");
    }
    defer {
        wl_display_disconnect(g_display);
        logInfo("Display destroyed.");
    };

    // Setup the registry
    {
        g_registry = wl_display_get_registry(g_display);
        g_registerListener.global = registerGlobal;
        g_registerListener.global_remove = registerGlobalRemove;
        wl_registry_add_listener(g_registry, &g_registerListener, &g_seat);
        wl_display_roundtrip(g_display); // Block until all pending request are processed by the server
    }

    // Setup compositor and surface
    {
        g_xdgWmBaseListener.ping = xdgWmBasePing;
        xdg_wm_base_add_listener(g_xdgWmBase, &g_xdgWmBaseListener, nullptr);

        g_surface = wl_compositor_create_surface(g_compositor);
        g_xdgSurface = xdg_wm_base_get_xdg_surface(g_xdgWmBase, g_surface);
        g_xdgToplevel = xdg_surface_get_toplevel(g_xdgSurface);

        g_xdgSurfaceListener.configure = handleConfigure;
        xdg_surface_add_listener(g_xdgSurface, &g_xdgSurfaceListener, nullptr);
    }

    // Setup the mouse event handler
    {
        g_pointer = wl_seat_get_pointer(g_seat);
        g_mouseListener.enter = mouseEnter,
        g_mouseListener.leave = mouseLeave,
        g_mouseListener.motion = mouseMotion,
        g_mouseListener.button = mouseButton,
        g_mouseListener.frame = mouseFrame,
        g_mouseListener.axis = mouseAxis,
        g_mouseListener.axis_source = mouseAxisSource,
        g_mouseListener.axis_stop = mouseAxisStop,
        g_mouseListener.axis_discrete = mouseAxisDiscrete,
        wl_pointer_add_listener(g_pointer, &g_mouseListener, nullptr);
    }

    wl_surface_commit(g_surface);
    wl_display_roundtrip(g_display);

    u16 v = 0;
    auto freq = core::getCPUFrequencyHz();

    i32 display_fd = wl_display_get_fd(g_display);
    struct pollfd pfd = { display_fd, POLLIN, 0 };

    // NOTE:
    // The main queue is dispatched by calling wl_display_dispatch(). This will dispatch any events queued on the main
    // queue and attempt to read from the display fd if its empty. Call is blocking !
    while (true) {
        u64 start = core::getPerfCounter();

        // Read from the Wayland socket without blocking
        int ret = wl_display_dispatch_pending(g_display);
        if (ret == -1) break;
        wl_display_flush(g_display);

        // Render Frame
        for (size_t i = 0; i < size; i+=4) {
            g_data[i] = u8(v%255); // blue
            g_data[i+1] = u8((v/2)%255); // green
            g_data[i+2] = u8((v/3)%255); // red
            g_data[i+3] = u8(255); // alpha
        }
        v++;

        wl_display_prepare_read(g_display);
        wl_surface_attach(g_surface, g_buffer, 0, 0);
        wl_surface_damage_buffer(g_surface, 0, 0, width, height);
        wl_surface_commit(g_surface);
        wl_display_flush(g_display);

        poll(&pfd, 1, 1);
        wl_display_read_events(g_display);

        u64 end = core::getPerfCounter();
        f64 elapsedMicros = f64(end - start) / f64(freq);
        f64 ahead = 1000.0/60.0 - elapsedMicros*1000.0;

        logInfo("elapsed={:f.6}ms", elapsedMicros*1000);
        logInfo("ahead={:f.6}ms", ahead);

        if (ahead > 0) {
            core::threadingSleep(u64(ahead));
        }
    }

    logInfo("Display created successfully.");

    return 0;
}
