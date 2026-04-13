#define _GNU_SOURCE

#include "debug.h"
#include "macro_magic.h"
#include "types.h"
#include "wl-client.h"
#include "wl-util.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <sys/mman.h>

#include "xdg-shell-client-protocol.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

// TODO: [VALGRIND] Investigate the valgrind report that lists some leaks and access patterns that are suspicious.
//                  One way to check this is to see whether these problems happen for glfw as well.
// TODO: [DECORATION] The current decoration rendering is double buffered by default. That should be configurable. Does
//                    it even need to be double buffered?

// A wl_display object represents a client connection to a Wayland compositor.
static struct wl_display* g_display = NULL;
// The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
static struct wl_registry* g_registry = NULL;
// The XDG WM base is the entry point for creating desktop-style windows in Wayland.
static struct xdg_wm_base* g_xdgWmBase = NULL;

// The compositor is the factory interface for creating surfaces and regions.
static struct wl_compositor* g_compositor = NULL;
// The global interface exposing sub-surface compositing capabilities. This is needed for the decoration subsurface.
static struct wl_subcompositor* g_subcompositor = NULL;

// A singleton global object that provides support for shared memory.
static struct wl_shm* g_shm = NULL;
// The preferred pixel format for software rendering.
static i32 g_preffered_pixel_format = -1;

#define WLCLIENT_MIN_DECORATION_HEIGHT 20

// State for all the open windows.
static wlclient_window_data g_windows[WLCLIENT_WINDOWS_COUNT] = {0};

// Backend hooks:
static void (*wlclient_backend_shutdown)(void);
static void (*wlclient_backend_destroy_window)(const wlclient_window* window);
static void (*wlclient_backend_resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height);

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

static void destroy_window_data(wlclient_window_data* wdata);
static void destroy_decoration_resources(wlclient_window_data* wdata);
static void destroy_decoration_buffers(wlclient_window_data* wdata);
static void destroy_edge_resources(wlclient_window_data* wdata);

static void resize_decoration(wlclient_window_data* wdata);
static void resize_decoration_shm_pool(wlclient_window_data* wdata, i32 new_pool_size);
static void recreate_decoration_buffers(wlclient_window_data* wdata, i32 buf_size, i32 pool_size);
static void render_decoration(wlclient_window_data* wdata);

static void update_framebuffer_size(const wlclient_window* window, wlclient_window_data* wdata);

//======================================================================================================================
// Wayland Handlers
//======================================================================================================================

static void register_global(void* data, struct wl_registry* wl_registry, u32 name, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 name);
static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor);
static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format);
static void release_buffer(void *data, struct wl_buffer *wl_buffer);

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial);
static void xdg_toplevel_close(void*, struct xdg_toplevel* toplevel);
static void xdg_toplevel_configure(void*, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states);
static void xdg_toplevel_configure_bounds(void*, struct xdg_toplevel* toplevel, i32 width, i32 height);
static void xdg_toplevel_configure_wm_capabilities(void*, struct xdg_toplevel* toplevel, struct wl_array* caps);

//======================================================================================================================
// Public
//======================================================================================================================

struct wl_display* wlclient_get_wl_display(void) {
    WLCLIENT_ASSERT(g_display);
    return g_display;
}

wlclient_window_data* wlclient_get_wl_window_data(const wlclient_window* window) {
    WLCLIENT_ASSERT(window && window->id >= 0 && window->id < WLCLIENT_WINDOWS_COUNT);
    wlclient_window_data* ret = &g_windows[window->id];
    WLCLIENT_ASSERT(ret->used);
    return ret;
}

void wlclient_set_backend_shutdown(void (*shutdown)(void)) {
    wlclient_backend_shutdown = shutdown;
}

void wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window)) {
    wlclient_backend_destroy_window = destroy_window;
}

void wlclient_set_backend_resize_window(void (*resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height)) {
    wlclient_backend_resize_window = resize_window;
}

void wlclient_get_window_size(const wlclient_window* window, i32* width, i32* height) {
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    if (width) *width = wdata->logical_width;
    if (height) *height = wdata->logical_height;
}

void wlclient_get_framebuffer_size(const wlclient_window* window, i32* width, i32* height) {
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    if (width) *width = wdata->framebuffer_pixel_width;
    if (height) *height = wdata->framebuffer_pixel_height;
}

void wlclient_set_close_handler(wlclient_window* window, wlclient_close_handler handler) {
    WLCLIENT_ASSERT(handler);
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    wdata->close_handler = handler;
}

void wlclient_toggle_decoration(wlclient_window* window) {
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    wdata->decoration_hide = !wdata->decoration_hide;

    if (wdata->decoration_hide) {
        wlclient_hide_surface(wdata->decoration_surface);
    }
    else {
        render_decoration(wdata);
    }
}

wlclient_error_code wlclient_init(void) {
    WLCLIENT_LOG_DEBUG("Initializing...");

    i32 ret = 0;

    g_display = wl_display_connect(NULL);
    if (!g_display) goto error;

    // Setup the global registry
    {
        g_registry = wl_display_get_registry(g_display);
        if (!g_registry) goto error;

        // The registry listener is a callback table for the global object registry.
        const static struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        ret = wl_registry_add_listener(g_registry, &registry_listener, NULL);
        if (ret != 0) goto error;

        // Block until all pending request to discover globals are processed.
        ret = wl_display_roundtrip(g_display);
        if (ret < 0) goto error;

        WLCLIENT_LOG_DEBUG("Registry initialized");
    }

    if (!g_compositor) goto error;
    if (!g_subcompositor) goto error;
    if (!g_xdgWmBase) goto error;
    if (!g_shm) goto error;

    // Do a second roundtrip to receive events emitted by newly bound globals (like wl_shm).
    ret = wl_display_roundtrip(g_display);
    if (ret < 0) goto error;

    if (g_preffered_pixel_format < 0) goto error;

    WLCLIENT_LOG_DEBUG("Initialization done");
    return WLCLIENT_ERROR_OK;

error:
    wlclient_shutdown();
    return WLCLIENT_ERROR_INIT_FAILED;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_DEBUG("Shutting down...");

    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        if (g_windows[i].used) {
            wlclient_window window = { .id = i };
            wlclient_destroy_window(&window);
        }
    }

    wlclient_backend_destroy_window = NULL;
    wlclient_backend_resize_window = NULL;

    if (wlclient_backend_shutdown) {
        wlclient_backend_shutdown();
        wlclient_backend_shutdown = NULL;
    }

    if (g_compositor) wl_compositor_destroy(g_compositor);
    if (g_subcompositor) wl_subcompositor_destroy(g_subcompositor);
    if (g_xdgWmBase) xdg_wm_base_destroy(g_xdgWmBase);
    if (g_shm) wl_shm_destroy(g_shm);
    if (g_registry) wl_registry_destroy(g_registry);
    if (g_display) wl_display_disconnect(g_display);

    g_compositor = NULL;
    g_subcompositor = NULL;
    g_xdgWmBase = NULL;
    g_shm = NULL;
    g_preffered_pixel_format = -1;
    g_registry = NULL;
    g_display = NULL;

    WLCLIENT_LOG_DEBUG("Shutdown");
}

wlclient_error_code wlclient_poll_events(void) {
    WLCLIENT_ASSERT(g_display);

    // Send all pending requests to the compositor first, otherwise the client might deadlock waiting on an event that
    // was never sent.
    {
        i32 ret = wl_display_flush(g_display);
        if (ret < 0 && errno != EAGAIN) goto error;
    }

    // Drain any events already queued in the client-side event queue before attempting to read more from the socket.
    // prepare_read fails while the queue is non-empty, so this loop is required.
    while (wl_display_prepare_read(g_display) != 0) {
        if (wl_display_dispatch_pending(g_display) < 0) goto error;
    }

    // Non-blocking poll for new data on the Wayland socket.
    struct pollfd pfd = {
        .fd = wl_display_get_fd(g_display),
        .events = POLLIN,
        .revents = 0,
    };
    i32 poll_ret = poll(&pfd, 1, 0);
    if (poll_ret < 0) {
        wl_display_cancel_read(g_display);
        goto error;
    }

    if (poll_ret > 0) {
        // Data available — read events from the socket into the queue.
        if (wl_display_read_events(g_display) < 0) goto error;
    }
    else {
        // No data — release the read lock without reading.
        wl_display_cancel_read(g_display);
    }

    // Dispatch events that were just read from the socket. read_events only deserializes wire data into
    // the queue without invoking callbacks — this second dispatch is what actually fires the listeners.
    if (wl_display_dispatch_pending(g_display) < 0) goto error;

    return WLCLIENT_ERROR_OK;

error:
    WLCLIENT_LOG_ERR("Wayland event dispatch failed");
    return WLCLIENT_ERROR_EVENT_DISPATCH_FAILED;
}

wlclient_error_code wlclient_create_window(i32 width, i32 height, const char* title, const wlclient_decoration_config* decor_cfg, wlclient_window* window) {
    i32 ret;
    wlclient_window_data* wdata = NULL;

    WLCLIENT_ASSERT(window);
    WLCLIENT_ASSERT(title);

    i32 decor_height = 0;
    if (decor_cfg && decor_cfg->decor_height > 0) {
        // Clamp decoration height to a minimum in case a decoration is configured.
        decor_height = WLCLIENT_MAX(decor_cfg->decor_height, WLCLIENT_MIN_DECORATION_HEIGHT);
    }

    i32 edge_border_size = 0;
    if (decor_cfg && decor_cfg->edge_border_size > 0) {
        edge_border_size = decor_cfg->edge_border_size;
    }

    WLCLIENT_LOG_DEBUG("Creating window -- width=%" PRIi32 ", height=%" PRIi32 ", title=%s", width, height, title);

    // Select an unused slot for the new window:
    window->id = -1;
    {
        for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
            if (!g_windows[i].used) {
                wdata = &g_windows[i];
                window->id = i;
                break;
            }
        }
        if (!wdata) {
            WLCLIENT_LOG_ERR("No more unused windows. Max allowed windows are %" PRIi32, WLCLIENT_WINDOWS_COUNT);
            goto error;
        }
    }

    // Create Surface
    {
        wdata->surface = wl_compositor_create_surface(g_compositor);
        if (!wdata->surface) goto error;

        const static struct wl_surface_listener surface_listener = {
            .enter = NULL, // TODO: The NULL listeners will crash if those events are ever triggered.
            .leave = NULL,
            .preferred_buffer_scale = surface_preferred_buffer_scale,
            .preferred_buffer_transform = NULL,
        };
        ret = wl_surface_add_listener(wdata->surface, &surface_listener, window);
        if (ret != 0) goto error;
    }

    // Create XDG Surface and TopLevel role.
    {
        wdata->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdgWmBase, wdata->surface);
        if (!wdata->xdg_surface) goto error;
        wdata->xdg_toplevel = xdg_surface_get_toplevel(wdata->xdg_surface);
        if (!wdata->xdg_toplevel) goto error;

        xdg_toplevel_set_title(wdata->xdg_toplevel, title);

        const static struct xdg_surface_listener xdg_surface_listener = {
            .configure = xdg_surface_configure
        };
        ret = xdg_surface_add_listener(wdata->xdg_surface, &xdg_surface_listener, window);
        if (ret != 0) goto error;

        const static struct xdg_toplevel_listener toplevel_listener = {
            .close = xdg_toplevel_close,
            .configure = xdg_toplevel_configure,
            .configure_bounds = xdg_toplevel_configure_bounds,
            .wm_capabilities = xdg_toplevel_configure_wm_capabilities,
        };
        ret = xdg_toplevel_add_listener(wdata->xdg_toplevel, &toplevel_listener, window);
        if (ret != 0) goto error;
    }

    // Create the decoration child surface after the parent has its toplevel role.
    if (decor_height > 0) {
        wdata->decoration_surface = wl_compositor_create_surface(g_compositor);
        if (!wdata->decoration_surface) goto error;

        wdata->decoration_subsurface = wl_subcompositor_get_subsurface(
            g_subcompositor,
            wdata->decoration_surface,
            wdata->surface
        );
        if (!wdata->decoration_subsurface) goto error;

        // Position the decoration directly above the main content surface.
        wl_subsurface_set_position(wdata->decoration_subsurface, 0, -decor_height);

        // Commit the decoration surface:
        wl_surface_commit(wdata->decoration_surface);
    }

    // Create invisible edge subsurfaces for interactive resize hit regions.
    if (edge_border_size > 0) {
        for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
            wdata->edge_surfaces[i].surface = wl_compositor_create_surface(g_compositor);
            if (!wdata->edge_surfaces[i].surface) goto error;

            wdata->edge_surfaces[i].subsurface = wl_subcompositor_get_subsurface(
                g_subcompositor,
                wdata->edge_surfaces[i].surface,
                wdata->surface
            );
            if (!wdata->edge_surfaces[i].subsurface) goto error;
        }

        // Position edges around the content area. Positions are relative to the main surface origin.
        wl_subsurface_set_position(
            wdata->edge_surfaces[WLCLIENT_EDGE_TOP].subsurface,
            -edge_border_size, -decor_height - edge_border_size
        );
        wl_subsurface_set_position(
            wdata->edge_surfaces[WLCLIENT_EDGE_BOTTOM].subsurface,
            -edge_border_size, height
        );
        wl_subsurface_set_position(
            wdata->edge_surfaces[WLCLIENT_EDGE_LEFT].subsurface,
            -edge_border_size, -decor_height
        );
        wl_subsurface_set_position(
            wdata->edge_surfaces[WLCLIENT_EDGE_RIGHT].subsurface,
            width, -decor_height
        );

        for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
            wl_surface_commit(wdata->edge_surfaces[i].surface);
        }
    }

    wdata->edge_border_size = edge_border_size;
    wdata->logical_width = width;
    wdata->logical_height = height + decor_height;
    wdata->content_logical_width = width;
    wdata->content_logical_height = height;
    wdata->buffer_scale = 1;
    wdata->framebuffer_pixel_width = width;
    wdata->framebuffer_pixel_height = height;
    wdata->decoration_hide = !(decor_height > 0);
    wdata->decoration_logical_height = decor_height;
    wdata->decoration_pixel_height = decor_height;
    wdata->decoration_anon_file_fd = -1; // Mark the initial decoration anonymous file as strictly invalid.
    wdata->decoration_pixel_width = width;
    wdata->used = true; // This needs to set before the round trip.
    wdata->pending = WLCLIENT_PENDING_DECORATION_RESIZE | WLCLIENT_PENDING_BACKEND_RESIZE;

    // Commit the main surface and roundtrip to synchronize the new window creation.
    // The roundtrip dispatches the first xdg_surface_configure, which creates the decoration buffers.
    wl_surface_commit(wdata->surface);
    ret = wl_display_roundtrip(g_display);
    if (ret < 0) goto error;

    WLCLIENT_LOG_DEBUG("Created successfully");
    return WLCLIENT_ERROR_OK;

error:
    window->id = -1;
    destroy_window_data(wdata);
    return WLCLIENT_ERROR_WINDOW_CREATE_FAILED;
}

void wlclient_destroy_window(wlclient_window* window) {
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);

    WLCLIENT_LOG_DEBUG("Destroying window with id=%" PRIi32 "...", window->id);
    if (wlclient_backend_destroy_window) {
        wlclient_backend_destroy_window(window);
    }
    destroy_window_data(wdata);
    window->id = -1;
    WLCLIENT_LOG_DEBUG("Destroyed");
}

//======================================================================================================================
// Helper Implementations
//======================================================================================================================

static void destroy_window_data(wlclient_window_data* wdata) {
    if (!wdata) return;

    if (wdata->xdg_toplevel) xdg_toplevel_destroy(wdata->xdg_toplevel);
    if (wdata->xdg_surface) xdg_surface_destroy(wdata->xdg_surface);

    destroy_decoration_resources(wdata);
    destroy_edge_resources(wdata);

    if (wdata->surface) wl_surface_destroy(wdata->surface);

    memset(wdata, 0, sizeof(*wdata));
}

static void destroy_decoration_resources(wlclient_window_data* wdata) {
    destroy_decoration_buffers(wdata);

    if (wdata->decoration_anon_file_fd >= 0) close(wdata->decoration_anon_file_fd);
    if (wdata->decoration_shm_pool) wl_shm_pool_destroy(wdata->decoration_shm_pool);
    if (wdata->decoration_subsurface) wl_subsurface_destroy(wdata->decoration_subsurface);
    if (wdata->decoration_surface) wl_surface_destroy(wdata->decoration_surface);
}

static void destroy_edge_resources(wlclient_window_data* wdata) {
    for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
        if (wdata->edge_surfaces[i].subsurface) wl_subsurface_destroy(wdata->edge_surfaces[i].subsurface);
        if (wdata->edge_surfaces[i].surface) wl_surface_destroy(wdata->edge_surfaces[i].surface);
    }
}

static void destroy_decoration_buffers(wlclient_window_data* wdata) {
    i32 buffers_count = (i32) WLCLIENT_ARRAY_SIZE(wdata->decoration_buffers);

    if (wdata->decoration_pixel_data) {
        usize pool_size = (usize)(wdata->decoration_pixel_width * WLCLIENT_BYTES_PER_PIXEL * wdata->decoration_pixel_height) * (usize) buffers_count;
        munmap(wdata->decoration_pixel_data, pool_size);
        wdata->decoration_pixel_data = NULL;
    }

    for (i32 i = 0; i < buffers_count; i++) {
        if (wdata->decoration_buffers[i]) {
            wl_buffer_destroy(wdata->decoration_buffers[i]);
            wdata->decoration_buffers[i] = NULL;
        }
        wdata->decoration_buffer_ready_states[i] = false;
    }
}

/**
* Create or resize the decoration's shared memory backing storage.
*
* The decoration uses a single memfd-backed wl_shm_pool that holds all double-buffer slots contiguously. Each slot is
* (width * BYTES_PER_PIXEL * height) bytes. Individual buffer regions are accessed at offset (buffer_index * slot_size)
* within decoration_pixel_data.
*
* On resize, the pool and fd are only recreated if the new size exceeds the current allocation. The wl_buffers, mmap,
* and pixel data are always recreated since they encode specific dimensions, but that should be a relatively cheap
* operation.
*/
static void resize_decoration(wlclient_window_data* wdata) {
    i32 new_decor_width = wdata->logical_width * wdata->buffer_scale;
    i32 new_decor_height = wdata->decoration_logical_height * wdata->buffer_scale;
    i32 decor_buffers_count = (i32) WLCLIENT_ARRAY_SIZE(wdata->decoration_buffers);
    i32 decor_buf_size = (new_decor_width * WLCLIENT_BYTES_PER_PIXEL) * new_decor_height;
    i32 decor_pool_size = decor_buf_size * decor_buffers_count;

    resize_decoration_shm_pool(wdata, decor_pool_size);

    destroy_decoration_buffers(wdata);
    wdata->decoration_pixel_width = new_decor_width;
    wdata->decoration_pixel_height = new_decor_height;
    recreate_decoration_buffers(wdata, decor_buf_size, decor_pool_size);

    // Hint to the compositor that the decoration region will be non-transparent.
    {
        struct wl_region* empty_region = wl_compositor_create_region(g_compositor);
        WLCLIENT_PANIC(empty_region, "wl_compositor_create_region failed");

        wl_region_add(empty_region, 0, 0, wdata->decoration_pixel_width, wdata->decoration_pixel_height);
        wl_surface_set_opaque_region(wdata->decoration_surface, empty_region);
        wl_region_destroy(empty_region);
    }
}

/**
* Re-create the temporary file and shm pool if the new decoration size won't fit. This logic only ever increases the
* size of the decoration buffer but never shrinks it.
*
* TODO: This strategy needs to be tested out for flickering or performance problems on initial resizes that make the
*       window bigger.
*/
static void resize_decoration_shm_pool(wlclient_window_data* wdata, i32 new_pool_size) {
    i32 decor_buffers_count = (i32) WLCLIENT_ARRAY_SIZE(wdata->decoration_buffers);
    i32 old_pool_size = (wdata->decoration_pixel_width * WLCLIENT_BYTES_PER_PIXEL) * wdata->decoration_pixel_height * decor_buffers_count;
    bool recreate = wdata->decoration_anon_file_fd < 0 || old_pool_size < new_pool_size;
    if (!recreate) {
        // existing anonymous file is large enough.
        return;
    }

    // Close the old anonymous file:
    if (wdata->decoration_anon_file_fd >= 0) {
        close(wdata->decoration_anon_file_fd);
        wdata->decoration_anon_file_fd = -1;
    }
    // Destroy the old pool:
    if (wdata->decoration_shm_pool) {
        wl_shm_pool_destroy(wdata->decoration_shm_pool);
        wdata->decoration_shm_pool = NULL;
    }

    // Create a new anonymous file for the shared pixel buffer:
    wdata->decoration_anon_file_fd = memfd_create("tmp", 0);
    WLCLIENT_PANIC(wdata->decoration_anon_file_fd >= 0, "Failed to create temporary anonymous file");

    i32 ret = ftruncate(wdata->decoration_anon_file_fd, new_pool_size);
    WLCLIENT_PANIC(ret == 0, "Failed to truncate temporary anonymous file");

    wdata->decoration_shm_pool = wl_shm_create_pool(g_shm, wdata->decoration_anon_file_fd, new_pool_size);
    WLCLIENT_PANIC(wdata->decoration_shm_pool, "Failed to create decoration shm pool");
}

static void recreate_decoration_buffers(wlclient_window_data* wdata, i32 buf_size, i32 pool_size) {
    i32 buffers_count = (i32) WLCLIENT_ARRAY_SIZE(wdata->decoration_buffers);

    wdata->decoration_pixel_data = mmap(
        NULL,
        (usize) pool_size,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        wdata->decoration_anon_file_fd,
        0
    );
    WLCLIENT_PANIC(wdata->decoration_pixel_data != MAP_FAILED, "Failed mmap decoration pixel data");
    memset(wdata->decoration_pixel_data, 0, (usize) pool_size);

    for (i32 i = 0; i < buffers_count; i++) {
        i32 offset = (i32) i * buf_size;
        wdata->decoration_buffers[i] = wl_shm_pool_create_buffer(
            wdata->decoration_shm_pool, offset,
            wdata->decoration_pixel_width, wdata->decoration_pixel_height, wdata->decoration_pixel_width * WLCLIENT_BYTES_PER_PIXEL,
            (u32) g_preffered_pixel_format
        );
        WLCLIENT_PANIC(wdata->decoration_buffers[i], "Failed to create decoration buffer from pool");

        static struct wl_buffer_listener listender = {
            .release = release_buffer,
        };
        i32 ret = wl_buffer_add_listener(wdata->decoration_buffers[i], &listender, &wdata->decoration_buffer_ready_states[i]);
        WLCLIENT_PANIC(ret == 0, "Failed to set release buffer listener");

        wdata->decoration_buffer_ready_states[i] = true;
    }
}

static void render_decoration(wlclient_window_data* wdata) {
    WLCLIENT_LOG_TRACE("Render decoration called...");

    if (wdata->decoration_logical_height <= 0) {
        WLCLIENT_LOG_WARN(
            "Decoration logical height is %"PRIi32"; which means \"do not render decoration\"",
            wdata->decoration_logical_height
        );
        return;
    }

    if (wdata->decoration_hide) {
        WLCLIENT_LOG_TRACE("Decoration hidden; will not render");
        return;
    }

    // Find a ready buffer
    i32 buf_idx = -1;
    {
        for (i32 i = 0; i < (i32) WLCLIENT_ARRAY_SIZE(wdata->decoration_buffers); i++) {
            if (wdata->decoration_buffer_ready_states[i]) {
                buf_idx = i;
                break;
            }
        }
        if (buf_idx < 0) {
            WLCLIENT_LOG_TRACE("No available buffers for decoration rendering..");
            return; // no buffer available
        }
    }

    WLCLIENT_LOG_TRACE("Buffer Index = %"PRIi32"", buf_idx);

    // TODO: Draw the actual decoration toolbar here instead of the hardcoded blue rect:
    i32 buf_size = (wdata->decoration_pixel_width * WLCLIENT_BYTES_PER_PIXEL) * wdata->decoration_pixel_height;
    i32 pixel_count = wdata->decoration_pixel_width * wdata->decoration_pixel_height;
    u8* pixels = wdata->decoration_pixel_data + (buf_idx * buf_size);
    for (i32 p = 0; p < pixel_count; p++) {
        pixels[p * WLCLIENT_BYTES_PER_PIXEL + 0] = 0xFF; // B
        pixels[p * WLCLIENT_BYTES_PER_PIXEL + 1] = 0x00; // G
        pixels[p * WLCLIENT_BYTES_PER_PIXEL + 2] = 0x00; // R
        pixels[p * WLCLIENT_BYTES_PER_PIXEL + 3] = 0xFF; // A
    }

    wdata->decoration_buffer_ready_states[buf_idx] = false;
    wl_surface_attach(wdata->decoration_surface, wdata->decoration_buffers[buf_idx], 0, 0);
    wl_surface_damage_buffer(wdata->decoration_surface, 0, 0, wdata->decoration_pixel_width, wdata->decoration_pixel_height);
    wl_surface_commit(wdata->decoration_surface);

    WLCLIENT_LOG_TRACE("Render decoration done.");
}

static void update_framebuffer_size(const wlclient_window* window, wlclient_window_data* wdata) {
    // TODO: [FRACTIONAL_SCALING] Add wp_fractional_scale_v1 + wp_viewporter support and compute framebuffer size from
    // the compositor's fractional preferred scale. The current wl_surface preferred_buffer_scale path is only the
    // integer fallback.

    i32 framebuffer_width = wdata->content_logical_width * wdata->buffer_scale;
    i32 framebuffer_height = wdata->content_logical_height * wdata->buffer_scale;

    if (framebuffer_width == wdata->framebuffer_pixel_width && framebuffer_height == wdata->framebuffer_pixel_height) {
        return;
    }

    wdata->framebuffer_pixel_width = framebuffer_width;
    wdata->framebuffer_pixel_height = framebuffer_height;

    WLCLIENT_LOG_TRACE(
        "Framebuffer size: content=%" PRIi32 "x%" PRIi32 ", scale=%" PRIi32 ", framebuffer=%" PRIi32 "x%" PRIi32,
        wdata->content_logical_width,
        wdata->content_logical_height,
        wdata->buffer_scale,
        wdata->framebuffer_pixel_width,
        wdata->framebuffer_pixel_height
    );

    if (wlclient_backend_resize_window && wdata->used) {
        wlclient_backend_resize_window(window, wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height);
    }
}

//======================================================================================================================
// Wayland Handlers Implementations
//======================================================================================================================

/**
* This is the mechanism for discovering compositor capabilities. Called when the compositor announces a new global
* interface.
*
* This happens:
*   - during initial registry enumeration (after roundtrip)
*   - when new globals appear at runtime
*
* Parameters:
*   data      - user-provided pointer passed to wl_registry_add_listener
*   registry  - the wl_registry instance
*   name      - unique numeric identifier for this global
*   interface - string name of the interface (e.g. "wl_compositor", "wl_seat")
*   version   - maximum supported version of the interface
*/
static void register_global(void* data, struct wl_registry* wl_registry, u32 name, const char* interface, u32 version) {
    (void)data;

    WLCLIENT_LOG_TRACE("Register global received for interface = %s", interface);

    if (strcmp(interface, "wl_compositor") == 0) {
        WLCLIENT_PANIC(!g_compositor, "wl_compositor re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) wl_compositor_interface.version, version);
        g_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, effective_version);
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        WLCLIENT_PANIC(!g_subcompositor, "wl_subcompositor re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) wl_subcompositor_interface.version, version);
        g_subcompositor = wl_registry_bind(wl_registry, name, &wl_subcompositor_interface, effective_version);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        WLCLIENT_PANIC(!g_xdgWmBase, "xdg_wm_base re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_xdgWmBase = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, effective_version);
        if (!g_xdgWmBase) return;

        // Setup ping handler
        static const struct xdg_wm_base_listener listender = {
            .ping = xdg_wm_base_ping
        };
        i32 ret = xdg_wm_base_add_listener(g_xdgWmBase, &listender, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup xdg base ping listener");
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        WLCLIENT_PANIC(!g_shm, "wl_shm re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) wl_shm_interface.version, version);
        g_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, effective_version);
        if (!g_shm) return;

        static const struct wl_shm_listener listener = {
            .format = shm_pick_format
        };
        i32 ret = wl_shm_add_listener(g_shm, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup shm format listener");
    }
}

/**
* This is the mechanism for learning that a previously advertised global interface has been removed from the registry.
*
* This happens:
*   - when a global disappears at runtime
*
* Parameters:
*   data      - user-provided pointer passed to wl_registry_add_listener
*   registry  - the wl_registry instance
*   name      - unique numeric identifier for the removed global
*/
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 name) {
    (void)data;
    (void)wl_registry;
    WLCLIENT_LOG_TRACE("Removing %" PRIu32, name);
}

/**
* This reports the compositor's preferred buffer scale for the surface. The client should render buffers at this
* scale and advertise it back with wl_surface_set_buffer_scale.
*
* This happens:
*   - after the surface becomes associated with outputs
*   - whenever the compositor's preferred buffer scale changes
*
* Parameters:
*   data    - user-provided wlclient_window pointer passed to wl_surface_add_listener
*   surface - the wl_surface instance
*   factor  - preferred buffer scale for the surface
*/
static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor) {
    wlclient_window* window = data;
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);

    WLCLIENT_ASSERT(factor > 0);
    if (wdata->buffer_scale == factor) return;

    WLCLIENT_LOG_TRACE("Preferred buffer scale: %" PRIi32, factor);

    wdata->buffer_scale = factor;
    wl_surface_set_buffer_scale(surface, factor);
    if (wdata->decoration_surface) {
        wl_surface_set_buffer_scale(wdata->decoration_surface, factor);
    }
    wdata->pending |= WLCLIENT_PENDING_DECORATION_RESIZE | WLCLIENT_PENDING_BACKEND_RESIZE;
}

/**
* This advertises one supported shared-memory pixel format for wl_shm buffers.
*
* This happens:
*   - after binding the wl_shm global
*   - once for each pixel format supported by the compositor
*
* Parameters:
*   data   - user-provided pointer passed to wl_shm_add_listener
*   wl_shm - the wl_shm instance
*   format - one advertised wl_shm_format value supported by the compositor
*/
static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format) {
    (void)data;
    (void)wl_shm;

    WLCLIENT_LOG_TRACE("Supported pixel format %" PRIu32, format);

    // The spec states that ARGB8888 and XRGB8888 should be supported by all renderers, so it is safe to just pick
    // ARGB8888 and never bother with anything else.
    if (format == WL_SHM_FORMAT_ARGB8888) {
        g_preffered_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
        WLCLIENT_LOG_DEBUG("Pixel format set to %" PRIu32, format);
    }
}

/**
* This signals that the compositor is done reading from the buffer and the client may reuse it.
*
* This happens:
*   - after the compositor finishes displaying a committed buffer
*
* Parameters:
*   data      - pointer to the corresponding decoration_buffer_ready_states entry
*   wl_buffer - the wl_buffer instance being released
*/
static void release_buffer(void *data, struct wl_buffer *wl_buffer) {
    (void)wl_buffer;

    WLCLIENT_LOG_TRACE("Buffer released");
    bool* ready = data;
    *ready = true;
}

/**
* This is the compositor liveness check for the xdg_wm_base object. The client must respond with a pong.
*
* This happens:
*   - when the compositor verifies that the client is responsive
*
* Parameters:
*   data        - user-provided pointer passed to xdg_wm_base_add_listener
*   xdg_wm_base - the xdg_wm_base instance
*   serial      - serial number that must be echoed back in xdg_wm_base_pong
*/
static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial) {
    (void)data;

    WLCLIENT_LOG_TRACE("Ping received serial: %" PRIu32, serial);
    xdg_wm_base_pong(xdg_wm_base, serial);
}

/**
* Handles the xdg_surface configure handshake. This is a synchronization point where the client applies any pending
* surface state changes for the configure and then acknowledges the serial. The ack must come after all state changes
* have been applied — it signals the compositor that the client is done processing this configure sequence.
*
* This happens:
*   - after the initial surface commit
*   - whenever the compositor sends a new surface configure event
*
* Parameters:
*   data        - user-provided wlclient_window pointer passed to xdg_surface_add_listener
*   xdg_surface - the xdg_surface instance
*   serial      - serial number that must be acknowledged with xdg_surface_ack_configure
*/
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial) {
    WLCLIENT_LOG_TRACE("Surface configure serial: %" PRIu32, serial);

    wlclient_window* window = data;
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);

    if (wdata->pending & WLCLIENT_PENDING_DECORATION_RESIZE) {
        if (wdata->decoration_logical_height > 0) {
            resize_decoration(wdata);

            xdg_surface_set_window_geometry(
                wdata->xdg_surface,
                0, -wdata->decoration_logical_height,
                wdata->logical_width, wdata->logical_height
            );

            render_decoration(wdata);
        }

    }

    if (wdata->pending & WLCLIENT_PENDING_BACKEND_RESIZE) {
        update_framebuffer_size(window, wdata);
    }

    wdata->pending = WLCLIENT_PENDING_NONE;

    // Acknowledge the current configuration handshake with the compositor.
    xdg_surface_ack_configure(xdg_surface, serial);
}

/**
* This is the compositor close request for the xdg_toplevel. It indicates that the window should begin shutdown.
*
* This happens:
*   - when the compositor requests that the window be closed
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*/
static void xdg_toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)toplevel;

    wlclient_window* window = data;
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    if (wdata->close_handler) {
        wdata->close_handler();
    }
}

/**
* This is the main configure event for the xdg_toplevel. It communicates the compositor's suggested logical size and
* state for the window.
*
* This happens:
*   - after the initial surface commit
*   - whenever the compositor updates the window size or state
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested logical window width, or 0 if unspecified
*   height    - suggested logical window height, or 0 if unspecified
*   states    - array of xdg_toplevel_state values describing the window state
*/
static void xdg_toplevel_configure(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states) {
    (void)toplevel;
    (void)states;

    wlclient_window* window = data;
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    i32 new_window_width = width > 0 ? width : wdata->logical_width;
    i32 new_window_height = height > 0 ? height : wdata->logical_height;
    i32 new_content_width = new_window_width;
    i32 new_content_height = WLCLIENT_MAX(new_window_height - wdata->decoration_logical_height, 1);

    WLCLIENT_LOG_TRACE(
        "Toplevel configure: window=%" PRIi32 "x%" PRIi32 ", content=%" PRIi32 "x%" PRIi32,
        new_window_width,
        new_window_height,
        new_content_width,
        new_content_height
    );

    if (new_window_width == wdata->logical_width && new_window_height == wdata->logical_height) {
        return;
    }

    if (new_window_width != wdata->logical_width) {
        // resize the decoration only when the logical width has changed.
        wdata->pending |= WLCLIENT_PENDING_DECORATION_RESIZE;
    }

    wdata->logical_width = new_window_width;
    wdata->logical_height = new_window_height;
    wdata->content_logical_width = new_content_width;
    wdata->content_logical_height = new_content_height;
    wdata->pending |= WLCLIENT_PENDING_BACKEND_RESIZE;
}

/**
* This reports compositor-provided upper bounds for the xdg_toplevel's logical size.
*
* This happens:
*   - when the compositor provides updated window bounds information
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested maximum logical window width, or 0 if unspecified
*   height    - suggested maximum logical window height, or 0 if unspecified
*/
static void xdg_toplevel_configure_bounds(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height) {
    (void)toplevel;

    WLCLIENT_LOG_TRACE("Toplevel bounds: size %" PRIi32 "x%" PRIi32, width, height);

    wlclient_window* window = data;
    wlclient_window_data* wdata = wlclient_get_wl_window_data(window);
    if (width > 0) wdata->max_logical_width = width;
    if (height > 0) wdata->max_logical_height = height;
}

/**
* This advertises the window management capabilities currently supported for the xdg_toplevel.
*
* This happens:
*   - when the compositor sends updated window management capabilities
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   caps      - array of xdg_toplevel_wm_capabilities values
*/
static void xdg_toplevel_configure_wm_capabilities(void* data, struct xdg_toplevel* toplevel, struct wl_array* caps) {
    (void)data;
    (void)toplevel;

    WLCLIENT_LOG_TRACE("Toplevel capabilities:");
    u32 *value;
    usize i = 0;
    wl_array_for_each(value, caps) {
        WLCLIENT_LOG_TRACE("  [%" PRIu64 "] = %" PRIu32, i++, *value);
    }
}
