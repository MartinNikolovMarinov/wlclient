#define _GNU_SOURCE

#include "debug.h"
#include "macro_magic.h"
#include "types.h"
#include "wl-client.h"
#include "wl-utils.h"

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#include "xdg-shell-client-protocol.h"
#include "wayland-client-protocol.h"

#include <wayland-client-core.h>
#include <wayland-util.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

// TODO: [FRACTIONAL_SCALING] Every calculation that uses scaling will have to change when fractional scaling is introduced.
// TODO: [ALPHA_BLENDING] The decoration alpha blending is broken, or possibly not configured; needs investigation.

#define ENSURE_OR_GOTO_ERR(expr)                                                    \
do {                                                                                \
    if (!(expr)) {                                                                  \
        _wlclient_report_wayland_fatal(g_state.display, #expr, __FILE__, __LINE__); \
        goto error;                                                                 \
    }                                                                               \
} while(0)

#define WLCLIENT_SHM_REQUIRED_VERSION 1

#define WLCLIENT_MAX_DECOR_HEIGHT 100
#define WLCLIENT_MAX_BORDER_THICKNESS 20

#define WLCLIENT_MAX_WINDOW_WIDTH 15360
#define WLCLIENT_MAX_WINDOW_HEIGHT 8640

// Mouse Input Flags:
#define MIF_NONE         (0u)
#define MIF_GAINED_FOCUS (1u << 0)
#define MIF_LOST_FOCUS   (1u << 1)
#define MIF_HAS_MOTION   (1u << 2)
#define MIF_IS_PRESSED   (1u << 3)
#define MIF_IS_RELEASED  (1u << 4)

static wlclient_global_state g_state;

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator);

static void destroy_all_input_devices(void);
static void destroy_input_device(wlclient_input_device* input_device);
static void destroy_input_device_seat(wlclient_input_device* input_device);
static void destroy_input_device_pointer(wlclient_input_device* input_device);
static void destroy_input_device_keyboard(wlclient_input_device* input_device);
static void destroy_input_device_xkb(wlclient_input_device* input_device);
static void store_new_input_device(u32 id, struct wl_seat* seat, u32 seat_version);
static wlclient_input_device* find_input_device_by_seat(struct wl_seat* seat);
static i32 find_input_device_index_by_id(u32 id);
static void remove_and_destroy_input_device_by_id(i32 idx);

static void destroy_window_data(wlclient_window_data* wdata);
static void log_window_data_dimensions(wlclient_window_data* wdata, wlclient_log_level level, const char* function_name);
static void apply_window_geometry(wlclient_window_data* wdata);
static void notify_window_resize(wlclient_window* window, wlclient_window_data* wdata);

static u32 content_to_window_width(const wlclient_window_data* wdata, u32 content_width);
static u32 content_to_window_height(const wlclient_window_data* wdata, u32 content_height);
static u32 window_to_content_width(const wlclient_window_data* wdata, u32 window_width);
static u32 window_to_content_height(const wlclient_window_data* wdata, u32 window_height);

static bool display_flush(struct wl_display* display);

static void surface_node_destroy(wlclient_surface_node* node);
static void surface_node_destroy_buffers(wlclient_surface_node* node);
static void surface_node_destroy_pools(wlclient_surface_node* node);
static void surface_node_create_buffers(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height);
static void surface_node_change_pool_size(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height);
static void surface_node_set_non_transperant_region(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height);
static void surface_node_render(wlclient_surface_node* node, wlclient_color color);

static bool decor_is_visible(wlclient_window_data* wdata);

static void decor_update_position(wlclient_window_data* wdata);
static void decor_handle_resize(wlclient_window_data* wdata);
static void decor_render(wlclient_window_data* wdata);
static void decor_hide(wlclient_window_data* wdata);

static void edges_update_position(wlclient_window_data* wdata);
static void edges_handle_resize(wlclient_window_data* wdata);
static void edges_render(wlclient_window_data* wdata);
static void edges_hide(wlclient_window_data* wdata);
static u32 edges_determine_xdg_toplevel_resize_edge(
    wlclient_window_data* wdata,
    wlclient_surface_type surface_type,
    f64 mouse_x
);

static wlclient_surface_type surface_get_type(wlclient_window_data* wdata, struct wl_surface* surface);
static u32 keyboard_translate_modifiers(wlclient_input_device* input_device);
static void keyboard_dispatch_text(wlclient_input_device* input_device, wlclient_window_data* wdata, wlclient_window* window, u32 keycode, u32 keysym);

//======================================================================================================================
// Wayland Handlers
//======================================================================================================================

static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id);

static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format);

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);

static void xdg_toplevel_close(void*, struct xdg_toplevel* toplevel);
static void xdg_toplevel_configure(void*, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states);
static void xdg_toplevel_configure_bounds(void*, struct xdg_toplevel* toplevel, i32 width, i32 height);
static void xdg_toplevel_configure_wm_capabilities(void*, struct xdg_toplevel* toplevel, struct wl_array* caps);

static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial);

static void seat_capabilities(void *data, struct wl_seat *wl_seat, u32 capabilities);
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name);

static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor);
static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void surface_preferred_buffer_transform(void *data, struct wl_surface *wl_surface, u32 transform);

static void release_buffer(void *data, struct wl_buffer *wl_buffer);

static void pointer_frame(void* data, struct wl_pointer* wl_pointer);
static void pointer_enter(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_leave(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface);
static void pointer_motion(void* data, struct wl_pointer* wl_pointer, u32 time, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_button(void* data, struct wl_pointer* wl_pointer, u32 serial, u32 time, u32 button, u32 state);
static void pointer_axis(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis, wl_fixed_t value);
static void pointer_axis_source(void* data, struct wl_pointer* wl_pointer, u32 axis_source);
static void pointer_axis_stop(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis);
static void pointer_axis_discrete(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 discrete);
static void pointer_axis_value120(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 value120);
static void pointer_axis_relative_direction(void* data, struct wl_pointer* wl_pointer, u32 axis, u32 direction);

static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard, u32 format, i32 fd, u32 size);
static void keyboard_enter(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface, struct wl_array* keys);
static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface);
static void keyboard_key(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 time, u32 key, u32 state);
static void keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group);
static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard, i32 rate, i32 delay);

//======================================================================================================================
// PUBLIC
//======================================================================================================================

wlclient_error_code wlclient_init(wlclient_allocator* allocator) {
    WLCLIENT_LOG_INFO("Initializing...");

    set_allocator(allocator);

    i32 ret = 0;

    g_state.display = wl_display_connect(NULL);
    if (!g_state.display) {
        WLCLIENT_LOG_FATAL("Failed to connect to display");
        return WLCLIENT_ERROR_INIT_FAILED;
    }

    // Make sure the pixel format is intentionally set to a value.
    g_state.preferred_pixel_format = -1;

    // Setup the global registry
    {
        g_state.registry = wl_display_get_registry(g_state.display);
        ENSURE_OR_GOTO_ERR(g_state.registry);

        static const struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        ret = wl_registry_add_listener(g_state.registry, &registry_listener, NULL);
        ENSURE_OR_GOTO_ERR(ret == 0);

        // Block until all pending request to discover globals are processed.
        ret = wl_display_roundtrip(g_state.display);
        ENSURE_OR_GOTO_ERR(ret >= 0);
    }

    // Verify all necessary globals are registered.
    ENSURE_OR_GOTO_ERR(g_state.compositor);
    ENSURE_OR_GOTO_ERR(g_state.subcompositor);
    ENSURE_OR_GOTO_ERR(g_state.xdgWmBase);
    ENSURE_OR_GOTO_ERR(g_state.shm);

    // Do a second roundtrip to receive events emitted by newly bound globals (like wl_shm preferred pixel format).
    ret = wl_display_roundtrip(g_state.display);
    ENSURE_OR_GOTO_ERR(ret >= 0);

    ENSURE_OR_GOTO_ERR(g_state.preferred_pixel_format >= 0);

    // Verify device inputs are configured correctly:
    {
        if (g_state.input_devices_count == 0) {
            WLCLIENT_LOG_WARN("No input devices detected");
        }

        for (i32 i = 0; i < g_state.input_devices_count; i++) {
            wlclient_input_device* d = &g_state.input_devices[i];
            ENSURE_OR_GOTO_ERR(d->used);
            ENSURE_OR_GOTO_ERR(d->seat);
            ENSURE_OR_GOTO_ERR(d->seat_id);

            // Technically a mouse and keyboard may have not been plugged in.
            // ENSURE_OR_GOTO_ERR(d->pointer);
            // ENSURE_OR_GOTO_ERR(d->keyboard);
        }
    }

    WLCLIENT_LOG_INFO("Initialization done");
    return WLCLIENT_ERROR_OK;

error:
    wlclient_shutdown();
    return WLCLIENT_ERROR_INIT_FAILED;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_INFO("Shutting down...");

    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        if (g_state.windows[i].used) {
            wlclient_window window = { .id = i };
            wlclient_destroy_window(&window);
        }
    }

    if (g_state.backend_shutdown) g_state.backend_shutdown();

    destroy_all_input_devices();

    if (g_state.compositor) wl_compositor_destroy(g_state.compositor);
    if (g_state.subcompositor) wl_subcompositor_destroy(g_state.subcompositor);
    if (g_state.xdgWmBase) xdg_wm_base_destroy(g_state.xdgWmBase);
    if (g_state.shm) wl_shm_destroy(g_state.shm);
    if (g_state.registry) wl_registry_destroy(g_state.registry);

    if (g_state.display) {
        // Flush the pending destory requests before disconnecting.
        bool ok = display_flush(g_state.display);
        WLCLIENT_ASSERT(ok, "Failed to flush on shutdown");
        wl_display_disconnect(g_state.display);
    }

    memset(&g_state, 0, sizeof(g_state));
    g_state.preferred_pixel_format = -1;

    WLCLIENT_LOG_INFO("Shutdown done");
}

wlclient_error_code wlclient_create_window(
    wlclient_window* window,
    u32 content_width, u32 content_height,
    const char* title,
    const wlclient_window_decoration_config* decor_cfg
) {
    i32 ret;
    wlclient_window_data* wdata = NULL;

    // Input Validation
    {
        WLCLIENT_PANIC(window, "window argument is null");
        WLCLIENT_PANIC(title, "title argument is null");
        WLCLIENT_PANIC(
            (decor_cfg->decor_logical_height == 0) == (decor_cfg->edge_logical_thickness == 0),
            "Decoration and edges should either both be 0 or both have a value"
        );
    }

    WLCLIENT_LOG_DEBUG("Creating window (%"PRIi32"x%"PRIi32")", content_width, content_height);

    // Select an unused slot for the new window:
    window->id = -1;
    {
        for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
            if (!g_state.windows[i].used) {
                wdata = &g_state.windows[i];
                window->id = i;
                break;
            }
        }
        ENSURE_OR_GOTO_ERR(wdata);
    }

    // Set the state that is need in some listener callback
    {
        wdata->decor_logical_height = 0;
        if (decor_cfg && decor_cfg->decor_logical_height > 0) {
            wdata->decor_logical_height = WLCLIENT_MIN(decor_cfg->decor_logical_height, WLCLIENT_MAX_DECOR_HEIGHT);
        }

        wdata->edge_logical_thickness = 0;
        if (decor_cfg && decor_cfg->edge_logical_thickness > 0) {
            wdata->edge_logical_thickness = WLCLIENT_MIN(decor_cfg->edge_logical_thickness, WLCLIENT_MAX_BORDER_THICKNESS);
        }

        wdata->decor_color = decor_cfg->decor_color;
        wdata->edge_color = decor_cfg->edge_color;

        wdata->scale_factor = 1.f;

        wdata->content_logical_width = content_width;
        wdata->content_logical_height = content_height;

        wdata->window_logical_width = content_to_window_width(wdata, wdata->content_logical_width);
        wdata->window_logical_height = content_to_window_height(wdata, wdata->content_logical_height);

        wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
        wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;

        wdata->used = true;
        wdata->csd_hidden = !(wdata->decor_logical_height > 0);

        // Set the file descriptors for each surface node to an explicitly wrong value.
        wdata->decor_node.anon_file_fd = -1;
        for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
            wdata->edge_nodes[i].anon_file_fd = -1;
        }
    }

    // Create Surface
    {
        wdata->surface = wl_compositor_create_surface(g_state.compositor);
        ENSURE_OR_GOTO_ERR(wdata->surface);

        // Associated the surface with the window
        wl_surface_set_user_data(wdata->surface, window);

        const static struct wl_surface_listener surface_listener = {
            .enter = surface_enter,
            .leave = surface_leave,
            .preferred_buffer_scale = surface_preferred_buffer_scale, // sets the scale factor
            .preferred_buffer_transform = surface_preferred_buffer_transform,
        };
        ret = wl_surface_add_listener(wdata->surface, &surface_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);
    }

    // Create XDG Surface and TopLevel role.
    {
        wdata->xdg_surface = xdg_wm_base_get_xdg_surface(g_state.xdgWmBase, wdata->surface);
        ENSURE_OR_GOTO_ERR(wdata->xdg_surface);
        wdata->xdg_toplevel = xdg_surface_get_toplevel(wdata->xdg_surface);
        ENSURE_OR_GOTO_ERR(wdata->xdg_toplevel);

        xdg_toplevel_set_title(wdata->xdg_toplevel, title);

        // TODO: [MIN_WINDOW_SIZE] allow setting minimin content size and calculate the window size from that.
        // xdg_toplevel_set_min_size(wdata->xdg_toplevel, (i32)min_w, (i32)min_h);

        const static struct xdg_surface_listener xdg_surface_listener = {
            .configure = xdg_surface_configure // synchronizes the in flight packets
        };
        ret = xdg_surface_add_listener(wdata->xdg_surface, &xdg_surface_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);

        const static struct xdg_toplevel_listener toplevel_listener = {
            .close = xdg_toplevel_close,
            .configure = xdg_toplevel_configure, // sets the in flight window logical width and height
            .configure_bounds = xdg_toplevel_configure_bounds,
            .wm_capabilities = xdg_toplevel_configure_wm_capabilities,
        };
        ret = xdg_toplevel_add_listener(wdata->xdg_toplevel, &toplevel_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);
    }

    if (wdata->decor_logical_height > 0) {
        // Create the decoration child surface and subsurface after the parent has its toplevel role.
        {
            wdata->decor_node.child_surface = wl_compositor_create_surface(g_state.compositor);
            ENSURE_OR_GOTO_ERR(wdata->decor_node.child_surface);

            wl_surface_set_user_data(wdata->decor_node.child_surface, window);

            wdata->decor_node.subsurface = wl_subcompositor_get_subsurface(
                g_state.subcompositor,
                wdata->decor_node.child_surface,
                wdata->surface
            );
            ENSURE_OR_GOTO_ERR(wdata->decor_node.subsurface);

            decor_update_position(wdata);
        }

        // Create edge subsurfaces for interactive resize hit regions.
        {
            for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
                wdata->edge_nodes[i].child_surface = wl_compositor_create_surface(g_state.compositor);
                ENSURE_OR_GOTO_ERR(wdata->edge_nodes[i].child_surface);

                wl_surface_set_user_data(wdata->edge_nodes[i].child_surface, window);

                wdata->edge_nodes[i].subsurface = wl_subcompositor_get_subsurface(
                    g_state.subcompositor,
                    wdata->edge_nodes[i].child_surface,
                    wdata->surface
                );
                ENSURE_OR_GOTO_ERR(wdata->edge_nodes[i].subsurface);
            }

            edges_update_position(wdata);
        }
    }

    // Final round trip to finalize window creation.
    wl_surface_commit(wdata->surface);
    ret = wl_display_roundtrip(g_state.display);
    ENSURE_OR_GOTO_ERR(ret >= 0);

    ENSURE_OR_GOTO_ERR(wdata->scale_factor > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_logical_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_logical_height > 0);
    ENSURE_OR_GOTO_ERR(wdata->content_logical_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->content_logical_height > 0);
    ENSURE_OR_GOTO_ERR(wdata->framebuffer_pixel_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->framebuffer_pixel_height > 0);

    // FIXME: Ensure that the decor node and edge nodes exist.

    WLCLIENT_LOG_DEBUG("Window creation done");
    return WLCLIENT_ERROR_OK;

error:
    window->id = -1;
    destroy_window_data(wdata);
    return WLCLIENT_ERROR_WINDOW_CREATE_FAILED;
}

void wlclient_destroy_window(wlclient_window* window) {
    if (!window || window->id < 0) return;

    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    WLCLIENT_LOG_DEBUG("Destroying window with id=%" PRIi32 "...", window->id);
    if (g_state.backend_destroy_window) {
        g_state.backend_destroy_window(window);
    }
    destroy_window_data(wdata);
    window->id = -1;
    WLCLIENT_LOG_DEBUG("Destroyed");
}

void wlclient_get_framebuffer(wlclient_window* window, u32* w, u32* h) {
    WLCLIENT_ASSERT(window, "window argument is null");
    WLCLIENT_ASSERT(w, "w argument is null");
    WLCLIENT_ASSERT(h, "h argument is null");
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    *w = wdata->framebuffer_pixel_width;
    *h = wdata->framebuffer_pixel_height;
}

wlclient_error_code wlclient_hide_decor(wlclient_window* window) {
    if (!window || window->id < 0) return WLCLIENT_ERROR_WINDOW_TOGGLE_DECOR_FAILED;

    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    WLCLIENT_ASSERT(wdata->used, "Attempt to toggle window that is unused");

    if (wdata->decor_logical_height == 0 || wdata->edge_logical_thickness == 0) {
        // Decoration not configured
        return WLCLIENT_ERROR_OK;
    }
    if (wdata->csd_hidden) {
        return WLCLIENT_ERROR_OK;
    }

    WLCLIENT_LOG_DEBUG("Hide window(id=%" PRIi32") client side decorations", window->id);

    wdata->csd_hidden = true;
    wdata->content_logical_width = wdata->window_logical_width;
    wdata->content_logical_height = wdata->window_logical_height;
    wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
    wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;
    apply_window_geometry(wdata);
    decor_hide(wdata);
    edges_hide(wdata);

    notify_window_resize(window, wdata);

    return WLCLIENT_ERROR_OK;
}

wlclient_error_code wlclient_show_decor(wlclient_window* window) {
    if (!window || window->id < 0) return WLCLIENT_ERROR_WINDOW_TOGGLE_DECOR_FAILED;

    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    WLCLIENT_ASSERT(wdata->used, "Attempt to toggle window that is unused");

    if (wdata->decor_logical_height == 0 || wdata->edge_logical_thickness == 0) {
        WLCLIENT_LOG_WARN("Can't show decoration because it isn't configured");
        return WLCLIENT_ERROR_WINDOW_TOGGLE_DECOR_FAILED;
    }
    if (!wdata->csd_hidden) {
        return WLCLIENT_ERROR_OK;
    }

    // If fullscreen decoration show does nothing but it should still has to set the csd_hidden flag.
    wdata->csd_hidden = false;
    if (wdata->is_fullscreen) {
        return WLCLIENT_ERROR_OK;
    }

    WLCLIENT_LOG_DEBUG("Show window(id=%" PRIi32") client side decorations", window->id);

    wdata->csd_hidden = false;
    wdata->content_logical_width = window_to_content_width(wdata, wdata->window_logical_width);
    wdata->content_logical_height = window_to_content_height(wdata, wdata->window_logical_height);
    wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
    wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;
    apply_window_geometry(wdata);
    decor_render(wdata);
    edges_render(wdata);

    notify_window_resize(window, wdata);

    return WLCLIENT_ERROR_OK;
}

WLCLIENT_API_EXPORT wlclient_error_code wlclient_poll_events(u64 timeout_ns) {
    bool received_event = false;
    enum { DISPLAY_FD }; // TODO: Will probably need an fd for cursor animation.
    struct pollfd pfds[] =
    {
        [DISPLAY_FD] = {.fd = wl_display_get_fd(g_state.display), .events = POLLIN, .revents = 0},
    };
    const i32 pfds_size = WLCLIENT_ARRAY_SIZE(pfds);

    while (!received_event)
    {
        // NOTE: Quote from the manual page:
        //
        // wl_display_prepare_read This function must be called before reading from the file descriptor using
        // wl_display_read_events(). Calling wl_display_prepare_read() announces the calling threads intention to read
        // and ensures that until the thread is ready to read and calls wl_display_read_events(), no other thread will
        // read from the file descriptor. This only succeeds if the event queue is empty though, and if there are
        // undispatched events in the queue, -1 is returned and errno set to EAGAIN.
        while (wl_display_prepare_read(g_state.display) != 0)
        {
            // NOTE: Quote from the manual page:
            //
            // wl_display_dispatch_pending is necessary when a client's main loop wakes up on some fd other than the
            // display fd (network socket, timer fd, etc) and calls wl_display_dispatch_queue() from that callback.
            // This may queue up events in the main queue while reading all data from the display fd.
            //
            // When the main thread returns to the main loop to block, the display fd no longer has data, causing a
            // call to poll(2) (or similar functions) to block indefinitely, even though there are events ready to
            // dispatch.
            //
            // The dispatch is what actually fires the listeners.
            i32 ret = wl_display_dispatch_pending(g_state.display);

            if (ret > 0) {
                // Dispatched at least one already-queued event, so return without blocking.
                goto done_ok;
            }
            if (ret < 0) {
                ENSURE_OR_GOTO_ERR(false);
            }
        }

        bool ok = display_flush(g_state.display);
        if (!ok) {
            wl_display_cancel_read(g_state.display);
            ENSURE_OR_GOTO_ERR(false);
        }

        struct wlclient_poll_result poll_result = wlclient_poll_with_timeout(pfds, pfds_size, timeout_ns);
        if (poll_result.poll_result < 0) {
            wl_display_cancel_read(g_state.display);
            ENSURE_OR_GOTO_ERR(false);
        }
        else if (poll_result.timedout) {
            wl_display_cancel_read(g_state.display);
            goto poll_timeout;
        }

        // NOTE: Quote from the manual page:
        //
        // If a thread successfully calls wl_display_prepare_read(), it must either call wl_display_read_events() when
        // it's ready or cancel the read intention by calling wl_display_cancel_read().
        if (pfds[DISPLAY_FD].revents & POLLIN) {
            // Read and queue events into their corresponding event queues:
            i32 dispatch_events_ret = wl_display_read_events(g_state.display);
            if (dispatch_events_ret < 0) {
                ENSURE_OR_GOTO_ERR(false);
            }

            // Dispatch events in a non-blocking manner. The dispatch is what actually fires the listeners.
            i32 dispatch_pending_ret = wl_display_dispatch_pending(g_state.display);
            if (dispatch_pending_ret > 0) {
                received_event = true;
            }
            else if (dispatch_pending_ret < 0) {
                ENSURE_OR_GOTO_ERR(false);
            }
        }
        else {
            wl_display_cancel_read(g_state.display);

            if (pfds[DISPLAY_FD].revents & (POLLERR | POLLHUP)) {
                // Display connection disconnect.
                ENSURE_OR_GOTO_ERR(false);
            }
        }
    }

done_ok:
    return WLCLIENT_ERROR_OK;
poll_timeout:
    return WLCLIENT_ERROR_EVENT_POLL_TIMEOUT;
error:
    return WLCLIENT_ERROR_EVENT_POLL_FAILED;
}

void wlclient_set_close_handler(wlclient_window* window, wlclient_close_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->close_handler = handler;
}

void wlclient_set_size_change_handler(wlclient_window* window, wlclient_size_change_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->size_change_handler = handler;
}

void wlclient_set_framebuffer_change_handler(wlclient_window* window, wlclient_framebuffer_change_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->framebuffer_change_handler = handler;
}

void wlclient_set_scale_factor_change_handler(wlclient_window* window, wlclient_scale_factor_change_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->scale_factor_change_handler = handler;
}

void wlclient_set_mouse_focus_handler(wlclient_window* window, wlclient_mouse_focus_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->mouse_focus_handler = handler;
}

void wlclient_set_mouse_move_handler(wlclient_window* window, wlclient_mouse_move_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->mouse_move_handler = handler;
}

void wlclient_set_mouse_press_handler(wlclient_window* window, wlclient_mouse_press_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->mouse_press_handler = handler;
}

void wlclient_set_keyboard_focus_handler(wlclient_window* window, wlclient_keyboard_focus_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->keyboard_focus_handler = handler;
}

void wlclient_set_keyboard_key_handler(wlclient_window* window, wlclient_keyboard_key_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->keyboard_key_handler = handler;
}

void wlclient_set_keyboard_text_handler(wlclient_window* window, wlclient_keyboard_text_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->keyboard_text_handler = handler;
}

void wlclient_set_keyboard_modifiers_handler(wlclient_window* window, wlclient_keyboard_modifiers_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->keyboard_modifiers_handler = handler;
}

void wlclient_set_keyboard_repeat_info_handler(wlclient_window* window, wlclient_keyboard_repeat_info_handler handler) {
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->keyboard_repeat_info_handler = handler;
}

//======================================================================================================================
// INTERNALS IMPLEMENTATIONS
//======================================================================================================================

struct wl_display* _wlclient_get_wl_display(void) {
    WLCLIENT_ASSERT(g_state.display, "display is null");
    return g_state.display;
}

struct wlclient_global_state* _wlclient_get_wl_global_state(void) {
    return &g_state;
}

wlclient_window_data* _wlclient_get_wl_window_data(const wlclient_window* window) {
    WLCLIENT_ASSERT(window && window->id >= 0 && window->id < WLCLIENT_WINDOWS_COUNT, "Invalid window argument");
    wlclient_window_data* ret = &g_state.windows[window->id];
    WLCLIENT_ASSERT(ret->used, "Window is marked as unused");
    return ret;
}

void _wlclient_set_backend_shutdown(void (*shutdown)(void)) {
    g_state.backend_shutdown = shutdown;
}

void _wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window)) {
    g_state.backend_destroy_window = destroy_window;
}

void _wlclient_set_backend_resize_framebuffer(void (*resize_fb)(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height)) {
    g_state.backend_resize_framebuffer = resize_fb;
}

void _wlclient_set_backend_scale_change(void (*scale_change)(const wlclient_window* window, f32 factor)) {
    g_state.backend_scale_change = scale_change;
}

//======================================================================================================================
// Helper Implementations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator) {
    if (allocator) {
        WLCLIENT_ASSERT(allocator->alloc, "The allocator interface did not set a alloc callback");
        WLCLIENT_ASSERT(allocator->free, "The allocator interface did not set a free callback");
        WLCLIENT_ASSERT(allocator->strdup, "The allocator interface did not set a strdup callback");

        g_state.allocator.alloc = allocator->alloc;
        g_state.allocator.free = allocator->free;
        g_state.allocator.strdup = allocator->strdup;
    }
    else {
        g_state.allocator.alloc = malloc;
        g_state.allocator.free = free;
        g_state.allocator.strdup = strdup;
    }
}

static u32 content_to_window_width(const wlclient_window_data* wdata, u32 content_width) {
    return content_width + (2 * wdata->edge_logical_thickness);
}

static u32 content_to_window_height(const wlclient_window_data* wdata, u32 content_height) {
    return content_height + (2 * wdata->edge_logical_thickness) + wdata->decor_logical_height;
}

static u32 window_to_content_width(const wlclient_window_data* wdata, u32 window_width) {
    u32 csd_width = 2 * wdata->edge_logical_thickness;
    return window_width > csd_width ? window_width - csd_width : 0;
}

static u32 window_to_content_height(const wlclient_window_data* wdata, u32 window_height) {
    u32 csd_height = (2 * wdata->edge_logical_thickness) + wdata->decor_logical_height;
    return window_height > csd_height ? window_height - csd_height : 0;
}

static void destroy_all_input_devices(void) {
    WLCLIENT_LOG_INFO("Destroying all input devices...");

    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        struct wlclient_input_device* input_device = &g_state.input_devices[i];
        destroy_input_device(input_device);
    }

    g_state.input_devices_count = 0;

    WLCLIENT_LOG_INFO("Input devices destroyed");
}

static void destroy_input_device(wlclient_input_device* input_device) {
    destroy_input_device_pointer(input_device);
    destroy_input_device_keyboard(input_device);
    destroy_input_device_seat(input_device);

    memset(input_device, 0, sizeof(*input_device));
}

static void destroy_input_device_seat(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->seat_name) {
        g_state.allocator.free(input_device->seat_name);
    }

    if (input_device->seat) {
        if (input_device->seat_version >= 5)
            wl_seat_release(input_device->seat);
        else
            wl_seat_destroy(input_device->seat);
    }

    input_device->seat = NULL;
    input_device->seat_name = NULL;
}

static void destroy_input_device_pointer(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->pointer) {
        if (input_device->seat_version >= 3)
            wl_pointer_release(input_device->pointer);
        else
            wl_pointer_destroy(input_device->pointer);
    }

    input_device->pointer = NULL;
}

static void destroy_input_device_keyboard(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->keyboard) {
        if (input_device->seat_version >= 3)
            wl_keyboard_release(input_device->keyboard);
        else
            wl_keyboard_destroy(input_device->keyboard);
    }

    input_device->keyboard = NULL;
    input_device->keyboard_target_surface = NULL;
    destroy_input_device_xkb(input_device);
}

static void destroy_input_device_xkb(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->xkb_compose_state) {
        xkb_compose_state_unref(input_device->xkb_compose_state);
    }
    if (input_device->xkb_compose_table) {
        xkb_compose_table_unref(input_device->xkb_compose_table);
    }
    if (input_device->xkb_state) {
        xkb_state_unref(input_device->xkb_state);
    }
    if (input_device->xkb_keymap) {
        xkb_keymap_unref(input_device->xkb_keymap);
    }
    if (input_device->xkb_context) {
        xkb_context_unref(input_device->xkb_context);
    }

    input_device->xkb_state = NULL;
    input_device->xkb_keymap = NULL;
    input_device->xkb_context = NULL;
    input_device->xkb_compose_table = NULL;
    input_device->xkb_compose_state = NULL;
}


static void store_new_input_device(u32 id, struct wl_seat* seat, u32 seat_version) {
    WLCLIENT_PANIC(
        g_state.input_devices_count < WLCLIENT_MAX_INPUT_DEVICES,
        "Maximum allowed seats (%"PRIi32") exceeded.",
        WLCLIENT_MAX_INPUT_DEVICES
    );

    wlclient_input_device new_device = {
        .seat_id = id,
        .seat = seat,
        .seat_version = seat_version,
        .used = true
    };

    g_state.input_devices[g_state.input_devices_count] = new_device;
    g_state.input_devices_count++;
}

static wlclient_input_device* find_input_device_by_seat(struct wl_seat* seat) {
    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        if (g_state.input_devices[i].seat == seat && g_state.input_devices[i].used) {
            return &g_state.input_devices[i];
        }
    }

    WLCLIENT_PANIC(false, "[BUG] Find failed; seat not registered");
    return NULL;
}

static i32 find_input_device_index_by_id(u32 id) {
    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        if (g_state.input_devices[i].seat_id == id) return i;
    }
    return -1;
}

static void remove_and_destroy_input_device_by_id(i32 idx) {
    WLCLIENT_PANIC(
        idx >= 0 && idx < g_state.input_devices_count,
        "[BUG] Remove failed; invalid index idx=%" PRIi32,
        idx
    );

    wlclient_input_device* input_device = &g_state.input_devices[idx];

    destroy_input_device(input_device);

    if (idx == g_state.input_devices_count - 1) {
        g_state.input_devices_count--;
        return;
    }

    i32 last = g_state.input_devices_count - idx - 1;
    if (last > 0) {
        memmove(
            &g_state.input_devices[idx],
            &g_state.input_devices[idx + 1],
            (usize)(last) * sizeof(*input_device)
        );
    }

    g_state.input_devices_count--;
}

static void destroy_window_data(wlclient_window_data* wdata) {
    if (!wdata) return;

    if (wdata->xdg_toplevel) xdg_toplevel_destroy(wdata->xdg_toplevel);
    if (wdata->xdg_surface) xdg_surface_destroy(wdata->xdg_surface);

    surface_node_destroy(&wdata->decor_node);
    for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
        surface_node_destroy(&wdata->edge_nodes[i]);
    }

    if (wdata->surface) wl_surface_destroy(wdata->surface);

    memset(wdata, 0, sizeof(*wdata));
}

static void log_window_data_dimensions(wlclient_window_data* wdata, wlclient_log_level level, const char* function_name) {
    _wlclient_log_message(
        level, function_name,
        "\n --- Window dimensions ---"
        "\n  scale_factor = %.3f,"
        "\n  csd = %s,"
        "\n  fullscreen = %s,"
        "\n  window_locical_size = %" PRIu32 "x%" PRIu32 ","
        "\n  content_locical_size = %" PRIu32 "x%" PRIu32
        "\n  framebuffer_size = %" PRIu32 "x%" PRIu32,
        (f64) wdata->scale_factor,
        wdata->csd_hidden ? "hidden" : "visible",
        wdata->is_fullscreen ? "true" : "false",
        wdata->window_logical_width, wdata->window_logical_height,
        wdata->content_logical_width, wdata->content_logical_height,
        wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height
    );
}

static void apply_window_geometry(wlclient_window_data* wdata) {
    i32 x, y, w, h;
    if (decor_is_visible(wdata)) {
        x = -(i32) wdata->edge_logical_thickness;
        y = -(i32) wdata->edge_logical_thickness - (i32) wdata->decor_logical_height;
        w = (i32) wdata->window_logical_width;
        h = (i32) wdata->window_logical_height;
    }
    else {
        x = 0;
        y = 0;
        w = (i32) wdata->window_logical_width;
        h = (i32) wdata->window_logical_height;
    }
    xdg_surface_set_window_geometry(wdata->xdg_surface, x, y, w, h);
}

// Notify backend and user code
static void notify_window_resize(wlclient_window* window, wlclient_window_data* wdata) {
    if (g_state.backend_resize_framebuffer && wdata->used) {
        g_state.backend_resize_framebuffer(window, wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height);
    }
    if (wdata->framebuffer_change_handler) {
        wdata->framebuffer_change_handler(window, wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height);
    }
    if (wdata->size_change_handler) {
        wdata->size_change_handler(window, wdata->content_logical_width, wdata->content_logical_height);
    }
}

static bool display_flush(struct wl_display* display) {
    bool res = true;

    errno = 0;

    while (wl_display_flush(display) < 0) {
        if (errno != EAGAIN) {
            res = false;
            break;
        }

        // Socket send buffer is likely full.
        // Block until the fd becomes writable again
        struct pollfd fd = { .fd = wl_display_get_fd(display), .events = POLLOUT, .revents = 0 };
        while (poll(&fd, 1, -1) < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                res = false;
                break;
            }
        }

        if (!(fd.revents & POLLOUT)) {
            // If POLLOUT is not set, the display connection is no longer writable and should be treated as failed.
            res = false;
            break;
        }
    }

    return res;
}

static void surface_node_destroy(wlclient_surface_node* node) {
    if (!node) return;

    surface_node_destroy_buffers(node);
    surface_node_destroy_pools(node);

    if (node->subsurface) wl_subsurface_destroy(node->subsurface);
    if (node->child_surface) wl_surface_destroy(node->child_surface);
}

static void surface_node_destroy_buffers(wlclient_surface_node* node) {
    if (node->pixel_data) {
        const usize stride = (usize) (node->pixel_width * WLCLIENT_BYTES_PER_PIXEL);
        usize map_size = stride * (usize) node->pixel_height * WLCLIENT_FRAMEBUFFERS_COUNT;
        munmap(node->pixel_data, map_size);
        node->pixel_data = NULL;
    }

    for (i32 i = 0; i < WLCLIENT_FRAMEBUFFERS_COUNT; i++) {
        if (node->buffers[i]) wl_buffer_destroy(node->buffers[i]);
        node->buffers[i] = NULL;
        node->ready_states[i] = false;
    }

    node->pixel_width = 0;
    node->pixel_height = 0;
}

static void surface_node_destroy_pools(wlclient_surface_node* node) {
    if (!node) return;

    // Close the old anonymous file:
    if (node->anon_file_fd >= 0) {
        close(node->anon_file_fd);
        node->anon_file_fd = -1;
    }
    // Destroy the old pool:
    if (node->shm_pool) {
        wl_shm_pool_destroy(node->shm_pool);
        node->shm_pool = NULL;
    }
}

static void surface_node_create_buffers(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height) {
    WLCLIENT_LOG_TRACE("Creating node buffers with sizes %"PRIu32"x%"PRIu32"", pixel_width, pixel_height);

    WLCLIENT_ASSERT(!node->pixel_data, "[LEAK] The pixel data should have been freed");

    u32 stride = pixel_width * WLCLIENT_BYTES_PER_PIXEL;
    u32 single_buffer_size = stride * pixel_height;
    u32 pool_size = single_buffer_size * WLCLIENT_FRAMEBUFFERS_COUNT;
    node->pixel_data = mmap(
        NULL,
        (u32) pool_size,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        node->anon_file_fd,
        0
    );
    WLCLIENT_PANIC(
        node->pixel_data != MAP_FAILED,
        "Failed mmap decoration pixel data with size %"PRIu32"",
        pool_size
    );
    memset(node->pixel_data, 0, pool_size);

    for (i32 i = 0; i < WLCLIENT_FRAMEBUFFERS_COUNT; i++) {
        i32 offset = i * (i32)single_buffer_size;

        WLCLIENT_ASSERT(!node->buffers[i], "[LEAK] All node buffers should have been destroyed.");

        node->buffers[i] = wl_shm_pool_create_buffer(
            node->shm_pool, offset,
            (i32) pixel_width, (i32) pixel_height,
            (i32) stride,
            (u32) g_state.preferred_pixel_format
        );
        WLCLIENT_PANIC(node->buffers[i], "Failed to create wl_buffer from pool");

        static struct wl_buffer_listener listener = {
            .release = release_buffer,
        };
        i32 ret = wl_buffer_add_listener(node->buffers[i], &listener, &node->ready_states[i]);
        WLCLIENT_PANIC(ret == 0, "Failed to set release buffer listener");

        node->ready_states[i] = true;
    }

    WLCLIENT_LOG_TRACE("Node buffers creation done");
}

/**
* Re-create the temporary file and shm pool if the new decoration size won't fit. This logic only ever increases the
* size of the decoration buffer but never shrinks it.
*/
static void surface_node_change_pool_size(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height) {
    WLCLIENT_LOG_TRACE(
        "Chaning pool size from %"PRIu32"x%"PRIu32" to %"PRIu32"x%"PRIu32"",
        node->pixel_width, node->pixel_height,
        pixel_width, pixel_height
    );

    WLCLIENT_ASSERT(node, "node argument is null");

    u32 old_stride = node->pixel_width * WLCLIENT_BYTES_PER_PIXEL;
    u32 new_stride = pixel_width * WLCLIENT_BYTES_PER_PIXEL;

    u32 old_size = old_stride * node->pixel_height * WLCLIENT_FRAMEBUFFERS_COUNT;
    u32 new_size = new_stride * pixel_height * WLCLIENT_FRAMEBUFFERS_COUNT;

    bool should_recreate = (node->anon_file_fd < 0) || (old_size < new_size);
    if (!should_recreate) {
        // existing anonymous file is large enough.
        return;
    }

    surface_node_destroy_pools(node);

    // Create a new anonymous file for the shared pixel buffer:
    node->anon_file_fd = memfd_create("surface-node-fd", 0);
    WLCLIENT_PANIC(node->anon_file_fd >= 0, "Failed to create temporary anonymous file");

    i32 ret = ftruncate(node->anon_file_fd, new_size);
    WLCLIENT_PANIC(ret == 0, "Failed to truncate temporary anonymous file");

    node->shm_pool = wl_shm_create_pool(g_state.shm, node->anon_file_fd, (i32)new_size);
    WLCLIENT_PANIC(node->shm_pool, "Failed to create shm pool");

    WLCLIENT_LOG_TRACE("Pool size change done");
}

// Hint to the compositor that the surface region will be non-transparent.
static void surface_node_set_non_transperant_region(wlclient_surface_node* node, u32 pixel_width, u32 pixel_height) {
    struct wl_region* empty_region = wl_compositor_create_region(g_state.compositor);
    WLCLIENT_PANIC(empty_region, "wl_compositor_create_region failed");

    wl_region_add(empty_region, 0, 0, (i32) pixel_width, (i32) pixel_height);
    wl_surface_set_opaque_region(node->child_surface, empty_region);
    wl_region_destroy(empty_region);
}

static void surface_node_render(wlclient_surface_node* node,  wlclient_color color) {
    WLCLIENT_LOG_TRACE("Render surface node called...");

    // Find a ready buffer
    i32 buf_idx = -1;
    {
        for (i32 i = 0; i < WLCLIENT_FRAMEBUFFERS_COUNT; i++) {
            if (node->ready_states[i]) {
                buf_idx = i;
                break;
            }
        }
    }
    if (buf_idx < 0) {
        WLCLIENT_LOG_WARN("No available buffer for surface node rendering...");
        return;
    }

    WLCLIENT_LOG_TRACE("Using buffer at index %"PRIi32, buf_idx);

    // TODO: [PERFORMANCE] This loop should be avoided if no actual width/height change happend for surface nodes?
    {
        u32 stride = node->pixel_width * WLCLIENT_BYTES_PER_PIXEL;
        u32 single_buffer_size = stride * node->pixel_height;
        u32 byte_offset = (u32) buf_idx * single_buffer_size;
        u32 pixel_count = node->pixel_width * node->pixel_height;
        u8* pixels = node->pixel_data + byte_offset;
        for (u32 p = 0; p < pixel_count; p++) {
            u32 idx = p * WLCLIENT_BYTES_PER_PIXEL;
            pixels[idx + 0] = color.b;
            pixels[idx + 1] = color.g;
            pixels[idx + 2] = color.r;
            pixels[idx + 3] = color.a;
        }
    }

    node->ready_states[buf_idx] = false;
    wl_surface_attach(node->child_surface, node->buffers[buf_idx], 0, 0);
    wl_surface_damage_buffer(node->child_surface, 0, 0, (i32) node->pixel_width, (i32) node->pixel_height);
    wl_surface_commit(node->child_surface);

    WLCLIENT_LOG_TRACE("Render surface node done.");
}

static bool decor_is_visible(wlclient_window_data* wdata) {
    return !wdata->is_fullscreen && !wdata->csd_hidden;
}

/**
* Position the decoration directly above the main content surface.
*/
static void decor_update_position(wlclient_window_data* wdata) {
    i32 decor_height = (i32)wdata->decor_logical_height * (i32)wdata->scale_factor;
    wl_subsurface_set_position(wdata->decor_node.subsurface, 0, -decor_height);
}

static void decor_handle_resize(wlclient_window_data* wdata) {
    if (wdata->decor_logical_height == 0) return;

    u32 new_decor_pixel_width = wdata->content_logical_width * (u32)wdata->scale_factor;
    u32 new_decor_pixel_height = wdata->decor_logical_height * (u32)wdata->scale_factor;

    bool no_resize_needed = new_decor_pixel_width == wdata->decor_node.pixel_width &&
                            new_decor_pixel_height == wdata->decor_node.pixel_height;
    if (no_resize_needed) {
        // Exactly the same dimensions -- no resize needed
        return;
    }

    surface_node_change_pool_size(&wdata->decor_node, new_decor_pixel_width, new_decor_pixel_height);

    // Re-create the buffers:
    surface_node_destroy_buffers(&wdata->decor_node);
    surface_node_create_buffers(&wdata->decor_node, new_decor_pixel_width, new_decor_pixel_height);

    surface_node_set_non_transperant_region(&wdata->decor_node, new_decor_pixel_width, new_decor_pixel_height);

    wdata->decor_node.pixel_width = new_decor_pixel_width;
    wdata->decor_node.pixel_height = new_decor_pixel_height;

    decor_update_position(wdata); // might not be necessary
}

static void decor_render(wlclient_window_data* wdata) {
    decor_handle_resize(wdata);
    if (decor_is_visible(wdata)) {
        WLCLIENT_LOG_DEBUG("Render decoration...");
        surface_node_render(&wdata->decor_node, wdata->decor_color);
        WLCLIENT_LOG_DEBUG("Render decoration done");
    }
    else {
        WLCLIENT_LOG_DEBUG("Decoration hidden; will not render");
    }
}

static void decor_hide(wlclient_window_data* wdata) {
    // Detach decoration surface:
    wl_surface_attach(wdata->decor_node.child_surface, NULL, 0, 0);
    wl_surface_commit(wdata->decor_node.child_surface);
}

/**
* Position the resize-edge subsurfaces relative to the main surface origin.
* Subsurface positions are cached state — the changes only take effect on the next parent surface commit.
*/
static void edges_update_position(wlclient_window_data* wdata) {
    WLCLIENT_ASSERT(wdata->edge_logical_thickness > 0, "Invalid edge logical thickness");

    i32 decor_height = (i32)wdata->decor_logical_height;
    i32 boarder_thickness = (i32)wdata->edge_logical_thickness;
    i32 bottom = (i32) wdata->content_logical_height;
    i32 right = (i32) wdata->content_logical_width;

    wl_subsurface_set_position(
        wdata->edge_nodes[WLCLIENT_EDGE_TOP].subsurface,
        -boarder_thickness,
        -decor_height - boarder_thickness
    );
    wl_subsurface_set_position(
        wdata->edge_nodes[WLCLIENT_EDGE_BOTTOM].subsurface,
        -boarder_thickness,
        bottom
    );
    wl_subsurface_set_position(
        wdata->edge_nodes[WLCLIENT_EDGE_LEFT].subsurface,
        -boarder_thickness,
        -decor_height
    );
    wl_subsurface_set_position(
        wdata->edge_nodes[WLCLIENT_EDGE_RIGHT].subsurface,
        right,
        -decor_height
    );
}

static void edges_handle_resize(wlclient_window_data* wdata) {
    if (wdata->edge_logical_thickness == 0) return;

    u32 edge_thickness = wdata->edge_logical_thickness * (u32)wdata->scale_factor;
    u32 edge_w = (wdata->content_logical_width + 2 * wdata->edge_logical_thickness) * (u32)wdata->scale_factor;
    u32 edge_h = (wdata->content_logical_height + wdata->decor_logical_height) * (u32)wdata->scale_factor;

    struct {
        u32 pixel_width;
        u32 pixel_height;
    } edge_sizes[] = {
        [WLCLIENT_EDGE_TOP]    = { .pixel_width = edge_w,         .pixel_height = edge_thickness },
        [WLCLIENT_EDGE_BOTTOM] = { .pixel_width = edge_w,         .pixel_height = edge_thickness },
        [WLCLIENT_EDGE_LEFT]   = { .pixel_width = edge_thickness, .pixel_height = edge_h },
        [WLCLIENT_EDGE_RIGHT]  = { .pixel_width = edge_thickness, .pixel_height = edge_h },
    };

    for (u32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
        wlclient_surface_node* edge_node = &wdata->edge_nodes[i];
        u32 new_edge_pixel_width = edge_sizes[i].pixel_width;
        u32 new_edge_pixel_height = edge_sizes[i].pixel_height;

        bool no_resize_needed = new_edge_pixel_width == edge_node->pixel_width &&
                                new_edge_pixel_height == edge_node->pixel_height;
        if (no_resize_needed) {
            // Exactly the same dimensions -- no resize needed
            continue;
        }

        surface_node_change_pool_size(edge_node, new_edge_pixel_width, new_edge_pixel_height);

        // Re-create the buffers:
        surface_node_destroy_buffers(edge_node);
        surface_node_create_buffers(edge_node, new_edge_pixel_width, new_edge_pixel_height);

        surface_node_set_non_transperant_region(edge_node, new_edge_pixel_width, new_edge_pixel_height);

        edge_node->pixel_width = new_edge_pixel_width;
        edge_node->pixel_height = new_edge_pixel_height;
    }

    edges_update_position(wdata);
}

static void edges_render(wlclient_window_data* wdata) {
    edges_handle_resize(wdata);
    if (decor_is_visible(wdata)) {
        WLCLIENT_LOG_DEBUG("Render edges...");
        for (u32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
            wlclient_surface_node* edge_node = &wdata->edge_nodes[i];
            surface_node_render(edge_node, wdata->edge_color);
        }
        WLCLIENT_LOG_DEBUG("Render edges done");
    }
    else {
        WLCLIENT_LOG_DEBUG("Edges hidden; will not render");
    }
}

static void edges_hide(wlclient_window_data* wdata) {
    // Detach edge surfaces:
    for (u32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
        wl_surface_attach(wdata->edge_nodes[i].child_surface, NULL, 0, 0);
        wl_surface_commit(wdata->edge_nodes[i].child_surface);
    }
}

static u32 edges_determine_xdg_toplevel_resize_edge(
    wlclient_window_data* wdata,
    wlclient_surface_type surface_type,
    f64 mouse_x
) {
    if (surface_type == ST_LEFT_EDGE)  return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    if (surface_type == ST_RIGHT_EDGE) return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;

    const f64 t = (f64)wdata->edge_logical_thickness;
    // Top/bottom edges span full window width, so their local x runs 0..window_logical_width.
    const f64 w = (f64)wdata->window_logical_width;

    if (surface_type == ST_TOP_EDGE) {
        if (mouse_x < t)     return XDG_TOPLEVEL_RESIZE_EDGE_TOP | XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        if (mouse_x > w - t) return XDG_TOPLEVEL_RESIZE_EDGE_TOP | XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    }

    if (surface_type == ST_BOTTOM_EDGE) {
        if (mouse_x < t)     return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM | XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        if (mouse_x > w - t) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM | XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    }

    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}

static wlclient_surface_type surface_get_type(wlclient_window_data* wdata, struct wl_surface* surface) {
    if (!surface) return ST_UNKNOWN;
    if (surface == wdata->surface) return ST_MAIN;
    if (surface == wdata->decor_node.child_surface) return ST_DECOR;
    if (surface == wdata->edge_nodes[WLCLIENT_EDGE_TOP].child_surface) return ST_TOP_EDGE;
    if (surface == wdata->edge_nodes[WLCLIENT_EDGE_BOTTOM].child_surface) return ST_BOTTOM_EDGE;
    if (surface == wdata->edge_nodes[WLCLIENT_EDGE_LEFT].child_surface) return ST_LEFT_EDGE;
    if (surface == wdata->edge_nodes[WLCLIENT_EDGE_RIGHT].child_surface) return ST_RIGHT_EDGE;
    return ST_UNKNOWN;
}

static u32 keyboard_translate_modifiers(wlclient_input_device* input_device) {
    if (!input_device || !input_device->xkb_state) return 0;

    u32 modifiers = 0;

    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= WLCLIENT_MOD_SHIFT;
    }
    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= WLCLIENT_MOD_CONTROL;
    }
    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= WLCLIENT_MOD_ALT;
    }
    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= WLCLIENT_MOD_SUPER;
    }
    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0) {
        modifiers |= WLCLIENT_MOD_CAPS_LOCK;
    }
    if (xkb_state_mod_name_is_active(input_device->xkb_state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0) {
        modifiers |= WLCLIENT_MOD_NUM_LOCK;
    }

    return modifiers;
}

static void keyboard_dispatch_text(
    wlclient_input_device* input_device,
    wlclient_window_data* wdata,
    wlclient_window* window,
    u32 keycode,
    u32 keysym
) {
    if (!input_device || !input_device->xkb_state || !wdata || !wdata->keyboard_text_handler) return;

    char utf8[64];

    if (input_device->xkb_compose_state && keysym != XKB_KEY_NoSymbol) {
        enum xkb_compose_feed_result feed_result = xkb_compose_state_feed(input_device->xkb_compose_state, keysym);
        if (feed_result == XKB_COMPOSE_FEED_ACCEPTED) {
            enum xkb_compose_status status = xkb_compose_state_get_status(input_device->xkb_compose_state);
            switch (status) {
                case XKB_COMPOSE_COMPOSED: {
                    i32 len = xkb_compose_state_get_utf8(input_device->xkb_compose_state, utf8, sizeof(utf8));
                    if (len > 0 && (usize)len < sizeof(utf8)) {
                        wdata->keyboard_text_handler(window, utf8, (usize)len);
                    }
                    xkb_compose_state_reset(input_device->xkb_compose_state);
                    return;
                }
                case XKB_COMPOSE_COMPOSING:
                    return;
                case XKB_COMPOSE_CANCELLED:
                    xkb_compose_state_reset(input_device->xkb_compose_state);
                    return;
                case XKB_COMPOSE_NOTHING:
                    break;
            }
        }
    }

    i32 len = xkb_state_key_get_utf8(input_device->xkb_state, keycode + 8, utf8, sizeof(utf8));
    if (len > 0 && (usize)len < sizeof(utf8)) {
        wdata->keyboard_text_handler(window, utf8, (usize)len);
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
*   id        - unique numeric identifier for this global
*   interface - string name of the interface (e.g. "wl_compositor", "wl_seat")
*   version   - maximum supported version of the interface
*/
static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version) {
    (void) data;

    WLCLIENT_LOG_TRACE("Register global received for interface = %s", interface);

    if (strcmp(interface, "wl_compositor") == 0) {
        WLCLIENT_PANIC(!g_state.compositor, "wl_compositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_compositor_interface.version, version);
        g_state.compositor = wl_registry_bind(wl_registry, id, &wl_compositor_interface, effective_version);
        WLCLIENT_PANIC(g_state.compositor, "failed to create wl_compositor");
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        WLCLIENT_PANIC(!g_state.subcompositor, "wl_subcompositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_subcompositor_interface.version, version);
        g_state.subcompositor = wl_registry_bind(wl_registry, id, &wl_subcompositor_interface, effective_version);
        WLCLIENT_PANIC(g_state.subcompositor, "failed to create wl_subcompositor");
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        WLCLIENT_PANIC(!g_state.xdgWmBase, "xdg_wm_base re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_state.xdgWmBase = wl_registry_bind(wl_registry, id, &xdg_wm_base_interface, effective_version);
        WLCLIENT_PANIC(g_state.xdgWmBase, "failed to create xdg_wm_base");

        static const struct xdg_wm_base_listener listener = {
            .ping = xdg_wm_base_ping
        };
        i32 ret = xdg_wm_base_add_listener(g_state.xdgWmBase, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup xdg base ping listener");
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        WLCLIENT_PANIC(!g_state.shm, "wl_shm re-registered");

        g_state.shm = wl_registry_bind(wl_registry, id, &wl_shm_interface, WLCLIENT_SHM_REQUIRED_VERSION);
        WLCLIENT_PANIC(g_state.shm, "failed to create wl_shm");

        static const struct wl_shm_listener listener = {
            .format = shm_pick_format
        };
        i32 ret = wl_shm_add_listener(g_state.shm, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup shm format listener");
    }
    else if (strcmp(interface, "wl_seat") == 0) {
        u32 effective_version = WLCLIENT_MIN((u32) wl_seat_interface.version, version);
        struct wl_seat* seat = wl_registry_bind(wl_registry, id, &wl_seat_interface, effective_version);
        WLCLIENT_PANIC(seat, "failed to create wl_seat");

        static const struct wl_seat_listener listener = {
            .capabilities = seat_capabilities,
            .name = seat_name,
        };
        i32 ret = wl_seat_add_listener(seat, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to add seat listener");

        store_new_input_device(id, seat, effective_version);
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
*   id        - unique numeric identifier for the removed global
*/
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id) {
    (void)data;
    (void)wl_registry;
    WLCLIENT_LOG_DEBUG("Removing %" PRIu32, id);

    {
        i32 idx = find_input_device_index_by_id(id);
        if (idx >= 0) remove_and_destroy_input_device_by_id(idx);
    }

    // TODO: [GLOBAL_OBJECT_REMOVE] Should any other objects be removed? GLFW seems to only handle monitors.
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
        g_state.preferred_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
        WLCLIENT_LOG_DEBUG("Pixel format set to %" PRIu32, format);
    }
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

    WLCLIENT_LOG_TRACE("Top level close");

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    if (wdata->close_handler) {
        wdata->close_handler(window);
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

    WLCLIENT_LOG_DEBUG("Top level configure size = %" PRIi32 "x%" PRIi32 "", width, height);

    WLCLIENT_ASSERT(width >= 0, "[SANITY_CHECK_FAILED] assumption about width being always positive is wrong.");
    WLCLIENT_ASSERT(height >= 0, "[SANITY_CHECK_FAILED] assumption about height being always positive is wrong.");

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wdata->toplevel_config_in_flight_packet.window_logical_width = (u32) width;
    wdata->toplevel_config_in_flight_packet.window_logical_height = (u32) height;

    wlclient_toplevel_state window_state = TOPLEVEL_STATE_NONE;
    enum xdg_toplevel_state *state;
    wl_array_for_each(state, states) {
        switch (*state) {
            case XDG_TOPLEVEL_STATE_MAXIMIZED: window_state |= TOPLEVEL_STATE_MAXIMIZED; break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN: window_state |= TOPLEVEL_STATE_FULLSCREEN; break;
            case XDG_TOPLEVEL_STATE_RESIZING: window_state |= TOPLEVEL_STATE_RESIZING; break;

            case XDG_TOPLEVEL_STATE_ACTIVATED: window_state |= TOPLEVEL_STATE_ACTIVATED; break;
            case XDG_TOPLEVEL_STATE_SUSPENDED: window_state |= TOPLEVEL_STATE_SUSPENDED; break;

            case XDG_TOPLEVEL_STATE_TILED_LEFT: window_state |= TOPLEVEL_STATE_TILED_LEFT; break;
            case XDG_TOPLEVEL_STATE_TILED_RIGHT: window_state |= TOPLEVEL_STATE_TILED_RIGHT; break;
            case XDG_TOPLEVEL_STATE_TILED_TOP: window_state |= TOPLEVEL_STATE_TILED_TOP; break;
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM: window_state |= TOPLEVEL_STATE_TILED_BOTTOM; break;
        }
    }

    WLCLIENT_LOG_DEBUG("Current window state = %d", window_state);
    wdata->toplevel_config_in_flight_packet.window_state = window_state;
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
    (void)data;
    (void)toplevel;
    WLCLIENT_LOG_DEBUG("Top level bounds size = %" PRIi32 "x%" PRIi32, width, height);
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
    WLCLIENT_LOG_DEBUG("Surface configure serial: %" PRIu32, serial);

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    wdata->is_fullscreen = wdata->toplevel_config_in_flight_packet.window_state & TOPLEVEL_STATE_FULLSCREEN;
    wdata->is_suspended = (wdata->toplevel_config_in_flight_packet.window_state & TOPLEVEL_STATE_SUSPENDED) != 0;

    if (wdata->toplevel_config_in_flight_packet.window_logical_width > 0) {
        wdata->window_logical_width = WLCLIENT_MIN(
            wdata->toplevel_config_in_flight_packet.window_logical_width,
            WLCLIENT_MAX_WINDOW_WIDTH
        );
    }
    if (wdata->toplevel_config_in_flight_packet.window_logical_height > 0) {
        wdata->window_logical_height = WLCLIENT_MIN(
            wdata->toplevel_config_in_flight_packet.window_logical_height,
            WLCLIENT_MAX_WINDOW_HEIGHT
        );
    }

    bool decor_visible = decor_is_visible(wdata);

    wdata->content_logical_width = decor_visible
        ? window_to_content_width(wdata, wdata->window_logical_width)
        : wdata->window_logical_width;
    wdata->content_logical_height = decor_visible
        ? window_to_content_height(wdata, wdata->window_logical_height)
        : wdata->window_logical_height;

    wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
    wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;

    log_window_data_dimensions(wdata, WLCLIENT_LOG_LEVEL_DEBUG, __func__);

    // At this point the window geometry is clear
    apply_window_geometry(wdata);

    // Acknowledge the current configuration handshake with the compositor.
    xdg_surface_ack_configure(xdg_surface, serial);

    if (decor_visible) {
        decor_render(wdata);
        edges_render(wdata);
    }
    else if (wdata->decor_logical_height > 0) {
        decor_hide(wdata);
        edges_hide(wdata);
    }

    notify_window_resize(window, wdata);

    // zero out the in-flight packet's width and height members.
    {
        wdata->toplevel_config_in_flight_packet.window_logical_width = 0;
        wdata->toplevel_config_in_flight_packet.window_logical_height = 0;
    }
}

/**
* This reports which input device classes are currently exposed by the seat.
*
* This happens:
*   - after binding the wl_seat global
*   - whenever the seat gains or loses capabilities such as pointer or keyboard
*
* Parameters:
*   data         - user-provided pointer passed to wl_seat_add_listener
*   wl_seat      - the wl_seat instance
*   capabilities - bitmask of wl_seat_capability values currently available on the seat
*/
static void seat_capabilities(void *data, struct wl_seat *wl_seat, u32 capabilities) {
    (void)data;

    WLCLIENT_LOG_DEBUG("Seat capabilities: %"PRIu32, capabilities);

    wlclient_input_device* input_device = find_input_device_by_seat(wl_seat);
    input_device->capabilities = capabilities;

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!input_device->keyboard) {
            input_device->keyboard = wl_seat_get_keyboard(input_device->seat);
            static const struct wl_keyboard_listener listener = {
                .enter = keyboard_enter,
                .leave = keyboard_leave,
                .key = keyboard_key,
                .keymap = keyboard_keymap,
                .modifiers = keyboard_modifiers,
                .repeat_info = keyboard_repeat_info,
            };
            i32 ret = wl_keyboard_add_listener(input_device->keyboard, &listener, input_device);
            WLCLIENT_PANIC(ret == 0, "Failed to add keyboard listener");

            WLCLIENT_LOG_DEBUG("Registered keyboard for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
    else {
        if (input_device->keyboard) {
            // Keyboard was unplugged (or compositor revoked the capability).
            WLCLIENT_LOG_DEBUG("Releasing keyboard for seat(id=%"PRIu32")", input_device->seat_id);
            destroy_input_device_keyboard(input_device);
        }
        else {
            WLCLIENT_LOG_DEBUG("No keyboard capability for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!input_device->pointer) {
            input_device->pointer = wl_seat_get_pointer(input_device->seat);
            static const struct wl_pointer_listener listener = {
                .enter = pointer_enter,
                .leave = pointer_leave,
                .motion = pointer_motion,
                .button = pointer_button,
                .axis = pointer_axis,
                .frame = pointer_frame,
                .axis_source = pointer_axis_source,
                .axis_stop = pointer_axis_stop,
                .axis_discrete = pointer_axis_discrete,
                .axis_value120 = pointer_axis_value120,
                .axis_relative_direction = pointer_axis_relative_direction,
            };
            i32 ret = wl_pointer_add_listener(input_device->pointer, &listener, input_device);
            WLCLIENT_PANIC(ret == 0, "Failed to add pointer listener");

            WLCLIENT_LOG_DEBUG("Registered mouse for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
    else {
        if (input_device->pointer) {
            // Mouse was unplugged (or compositor revoked the capability).
            WLCLIENT_LOG_DEBUG("Releasing mouse for seat(id=%"PRIu32")", input_device->seat_id);
            destroy_input_device_pointer(input_device);
        }
        else {
            WLCLIENT_LOG_DEBUG("No mouse capability for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
}

/**
* This reports the compositor-provided descriptive name for the seat.
*
* This happens:
*   - after binding the wl_seat global when the compositor advertises a seat name
*   - whenever the compositor updates the seat name
*
* Parameters:
*   data    - user-provided pointer passed to wl_seat_add_listener
*   wl_seat - the wl_seat instance
*   name    - human-readable seat name supplied by the compositor
*/
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data;

    WLCLIENT_LOG_DEBUG("Seat name: %s", name);

    wlclient_input_device* input_device = find_input_device_by_seat(wl_seat);
    if (input_device->seat_name) {
        g_state.allocator.free(input_device->seat_name);
    }
    input_device->seat_name = g_state.allocator.strdup(name);

    WLCLIENT_PANIC(input_device->seat_name, "Failed to allocate memory for the seat name");
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
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    WLCLIENT_LOG_DEBUG("Preferred buffer scale: %" PRIi32, factor);

    if ((i32)wdata->scale_factor == factor) return;

    wdata->scale_factor = (f32)factor;
    wl_surface_set_buffer_scale(surface, (i32) wdata->scale_factor);

    // Notify backend and user
    {
        if (g_state.backend_scale_change) {
            g_state.backend_scale_change(window, wdata->scale_factor);
        }
        if (wdata->scale_factor_change_handler) {
            wdata->scale_factor_change_handler(window, wdata->scale_factor);
        }
    }
}

/**
* This notifies the client that the surface has entered the region of an output.
*
* This happens:
*   - when any part of the surface first becomes visible on a given wl_output
*   - when the surface is moved or resized so that it overlaps a new wl_output
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   output     - the wl_output the surface has entered
*/
static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    (void)data;
    (void)wl_surface;
    (void)output;

    WLCLIENT_LOG_DEBUG("Surface enter");
}

/**
* This notifies the client that the surface has left the region of an output.
*
* This happens:
*   - when the surface no longer overlaps a wl_output it previously entered
*   - when the output is unplugged or otherwise removed
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   output     - the wl_output the surface has left
*/
static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    (void)data;
    (void)wl_surface;
    (void)output;

    WLCLIENT_LOG_DEBUG("Surface leave");
}

/**
* This advertises the buffer transform the compositor prefers for this surface.
*
* This happens:
*   - when the compositor determines an optimal wl_output_transform for attached buffers
*   - when that preferred transform changes (e.g. output rotation)
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   transform  - wl_output_transform value the compositor prefers for buffers
*/
static void surface_preferred_buffer_transform(void *data, struct wl_surface *wl_surface, u32 transform) {
    (void)data;
    (void)wl_surface;
    (void)transform;

    WLCLIENT_LOG_DEBUG("Surface preferred buffer transformaiton");
}

/**
* This signals that the compositor is done reading from the decoration buffer and the client may reuse it.
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
* This marks the end of a batch of pointer events.
*
* This happens:
*   - after the compositor groups related pointer events into a frame
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*/
static void pointer_frame(void* data, struct wl_pointer* wl_pointer) {
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer frame");

    wlclient_input_device* idev = data;
    u32 flags = idev->mouse_in_flight_packet.pending_flags;

    // Dispatch focus-lost first, on the surface that was actually left.
    if ((flags & MIF_LOST_FOCUS) && idev->mouse_in_flight_packet.leave_surface) {
        struct wl_surface* leave_surface = idev->mouse_in_flight_packet.leave_surface;
        wlclient_surface_type leave_type = idev->mouse_in_flight_packet.leave_surface_type;
        wlclient_window* leave_window = wl_surface_get_user_data(leave_surface);
        wlclient_window_data* leave_wdata = _wlclient_get_wl_window_data(leave_window);

        if (leave_type == ST_MAIN && leave_wdata->mouse_focus_handler) {
            leave_wdata->mouse_focus_handler(leave_window, false);
        }
    }

    // Dispatch everything else against the currently-entered surface.
    struct wl_surface* target_surface = idev->mouse_in_flight_packet.target_surface;
    if (target_surface) {
        wlclient_surface_type target_type = idev->mouse_in_flight_packet.target_surface_type;
        wlclient_window* target_window = wl_surface_get_user_data(target_surface);
        wlclient_window_data* wdata = _wlclient_get_wl_window_data(target_window);

        u32 button = idev->mouse_in_flight_packet.button;
        f64 x = idev->mouse_in_flight_packet.x;
        f64 y = idev->mouse_in_flight_packet.y;

        WLCLIENT_LOG_DEBUG(
            "\n --- Pointer frame ---"
            "\n  window_id = %"PRIi32","
            "\n  surface_type = %"PRIu32","
            "\n  position = (x=%.3f, y=%.3f),"
            "\n  has_motion = %s,"
            "\n  button = %"PRIu32","
            "\n  button_is_pressed = %s,"
            "\n  button_is_released = %s,"
            "\n  focus_gained = %s",
            target_window->id,
            target_type,
            x, y,
            (flags & MIF_HAS_MOTION) ? "true" : "false",
            button,
            (flags & MIF_IS_PRESSED) ? "true" : "false",
            (flags & MIF_IS_RELEASED) ? "true" : "false",
            (flags & MIF_GAINED_FOCUS) ? "true" : "false"
        );

        switch (target_type) {
            case ST_MAIN: {
                if (wdata->mouse_focus_handler && (flags & MIF_GAINED_FOCUS)) {
                    wdata->mouse_focus_handler(target_window, true);
                }
                if (wdata->mouse_move_handler && (flags & MIF_HAS_MOTION)) {
                    wdata->mouse_move_handler(target_window, x, y);
                }
                if (wdata->mouse_press_handler && (flags & MIF_IS_PRESSED)) {
                    wdata->mouse_press_handler(target_window, button, true, x, y);
                }
                if (wdata->mouse_press_handler && (flags & MIF_IS_RELEASED)) {
                    wdata->mouse_press_handler(target_window, button, false, x, y);
                }
                break;
            }

            case ST_DECOR: {
                if ((flags & MIF_IS_PRESSED) && button == BTN_LEFT) {
                    // Start an interactive, user-driven move of the surface when the left mouse button is presesd over
                    // the decoration surface.
                    xdg_toplevel_move(
                        wdata->xdg_toplevel,
                        idev->seat,
                        idev->mouse_in_flight_packet.button_serial
                    );
                }
                break;
            }
            case ST_TOP_EDGE:
            case ST_RIGHT_EDGE:
            case ST_BOTTOM_EDGE:
            case ST_LEFT_EDGE: {
                if ((flags & MIF_IS_PRESSED) && button == BTN_LEFT) {
                    // Start an interactive, user-driven resize of the surface when the left mouse button is presesd
                    // over one of the decoration's edge surfaces.
                    u32 edge = edges_determine_xdg_toplevel_resize_edge(wdata, target_type, x);
                    xdg_toplevel_resize(
                        wdata->xdg_toplevel,
                        idev->seat,
                        idev->mouse_in_flight_packet.button_serial,
                        edge
                    );
                }
                break;
            }

            case ST_UNKNOWN:
            case ST_SENTINEL:
                WLCLIENT_LOG_WARN("Pointer frame on unknown surface type");
                break;
        }
    }

    // Reset certain state for this frame
    {
        idev->mouse_in_flight_packet.pending_flags = MIF_NONE;
        idev->mouse_in_flight_packet.leave_surface = NULL;
        idev->mouse_in_flight_packet.leave_surface_type = ST_UNKNOWN;
    }
}

/**
* This notifies the client that pointer focus entered one of its surfaces.
*
* This happens:
*   - when the compositor grants pointer focus to the client surface
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial    - serial identifying the enter event
*   surface   - the wl_surface that received pointer focus
*   surface_x - pointer x coordinate in surface-local coordinates
*   surface_y - pointer y coordinate in surface-local coordinates
*/
static void pointer_enter(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)serial;
    (void)wl_pointer;

    wlclient_window* window = wl_surface_get_user_data(surface);
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wlclient_input_device* idev = data;
    wlclient_surface_type stype = surface_get_type(wdata, surface);

    idev->mouse_in_flight_packet.target_surface = surface;
    idev->mouse_in_flight_packet.target_surface_type = stype;
    idev->mouse_in_flight_packet.pending_flags |= MIF_GAINED_FOCUS;
    idev->mouse_in_flight_packet.x = wl_fixed_to_double(surface_x);
    idev->mouse_in_flight_packet.y = wl_fixed_to_double(surface_y);

    WLCLIENT_LOG_DEBUG(
        "Pointer entered at surface type=%"PRIu32" for window(id=%"PRIu32") at position: x=%f, y=%f",
        stype,
        window->id,
        wl_fixed_to_double(surface_x),
        wl_fixed_to_double(surface_y)
    );
}

/**
* This notifies the client that pointer focus left one of its surfaces.
*
* This happens:
*   - when the compositor removes pointer focus from the client surface
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial     - serial identifying the leave event
*   surface    - the wl_surface that lost pointer focus
*/
static void pointer_leave(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface) {
    (void)serial;
    (void)wl_pointer;

    wlclient_window* window = wl_surface_get_user_data(surface);
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wlclient_input_device* idev = data;
    wlclient_surface_type stype = surface_get_type(wdata, surface);

    // Record the surface losing focus in its own slot. If an enter follows in the same frame it will populate
    // target_surface separately, so both dispatches survive.
    idev->mouse_in_flight_packet.leave_surface = surface;
    idev->mouse_in_flight_packet.leave_surface_type = stype;
    idev->mouse_in_flight_packet.pending_flags |= MIF_LOST_FOCUS;
    // After leave, there's no currently-entered surface unless an enter follows in the same frame.
    idev->mouse_in_flight_packet.target_surface = NULL;
    idev->mouse_in_flight_packet.target_surface_type = ST_UNKNOWN;

    WLCLIENT_LOG_DEBUG("Pointer left surface type=%"PRIu32" on window(id=%"PRIu32")", stype, window->id);
}

/**
* This reports pointer motion in surface-local coordinates.
*
* This happens:
*   - whenever the pointer moves while focused on one of the client's surfaces
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time       - compositor timestamp in milliseconds
*   surface_x  - pointer x coordinate in surface-local coordinates
*   surface_y  - pointer y coordinate in surface-local coordinates
*/
static void pointer_motion(void* data, struct wl_pointer* wl_pointer, u32 time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)time;
    (void)wl_pointer;

    f64 x = wl_fixed_to_double(surface_x);
    f64 y = wl_fixed_to_double(surface_y);

    wlclient_input_device* idev = data;
    idev->mouse_in_flight_packet.pending_flags |= MIF_HAS_MOTION;
    idev->mouse_in_flight_packet.x = x;
    idev->mouse_in_flight_packet.y = y;

    WLCLIENT_LOG_TRACE("Pointer motion x=%f, y=%f", x, y);
}

/**
* This reports a pointer button press or release.
*
* This happens:
*   - whenever a focused pointer button changes state
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial    - serial identifying the button event
*   time      - compositor timestamp in milliseconds
*   button    - linux input button code
*   state     - wl_pointer_button_state value for press or release
*/
static void pointer_button(void* data, struct wl_pointer* wl_pointer, u32 serial, u32 time, u32 button, u32 state) {
    (void)wl_pointer;
    (void)time;

    wlclient_input_device* idev = data;
    idev->mouse_in_flight_packet.pending_flags &= ~(MIF_IS_PRESSED | MIF_IS_RELEASED); // unset both
    idev->mouse_in_flight_packet.pending_flags |=
        (state == WL_POINTER_BUTTON_STATE_PRESSED) ? MIF_IS_PRESSED : MIF_IS_RELEASED; // set one
    idev->mouse_in_flight_packet.button = button;
    idev->mouse_in_flight_packet.button_serial = serial;

    WLCLIENT_LOG_TRACE(
        "Pointer button: serial=%" PRIu32 ", time=%" PRIu32 ", button=%" PRIu32 ", state=%" PRIu32,
        serial, time, button, state
    );
}

/**
* This reports continuous scroll-axis motion for the focused pointer.
*
* This happens:
*   - when the compositor sends scroll deltas for a pointer axis
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time      - compositor timestamp in milliseconds
*   axis      - wl_pointer_axis value identifying the scroll axis
*   value     - fixed-point axis delta in compositor-defined units
*/
static void pointer_axis(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis, wl_fixed_t value) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE(
        "Pointer axis: time=%" PRIu32 ", axis=%" PRIu32 ", value=%f",
        time, axis, wl_fixed_to_double(value)
    );
}

/**
* This identifies the source device that produced the current pointer axis events.
*
* This happens:
*   - when the compositor provides axis source metadata for a scroll sequence
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis_source - wl_pointer_axis_source value describing the source device
*/
static void pointer_axis_source(void* data, struct wl_pointer* wl_pointer, u32 axis_source) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis source: %" PRIu32, axis_source);
}

/**
* This signals the end of scrolling activity for a specific pointer axis.
*
* This happens:
*   - when a pointer axis sequence terminates
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time       - compositor timestamp in milliseconds
*   axis       - wl_pointer_axis value identifying the stopped axis
*/
static void pointer_axis_stop(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis stop: time=%" PRIu32 ", axis=%" PRIu32, time, axis);
}

/**
* This reports deprecated discrete scroll steps for a pointer axis.
*
* This happens:
*   - when the compositor provides integer wheel-step data for scrolling
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   discrete   - integer number of discrete scroll steps
*/
static void pointer_axis_discrete(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 discrete) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis discrete: axis=%" PRIu32 ", discrete=%" PRIi32, axis, discrete);
}

/**
* This reports high-resolution discrete scroll data in units of 120 per wheel detent.
*
* This happens:
*   - when the compositor sends wl_pointer.axis_value120 events
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   value120   - high-resolution discrete delta where 120 equals one notch
*/
static void pointer_axis_value120(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 value120) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis value120: axis=%" PRIu32 ", value120=%" PRIi32, axis, value120);
}

/**
* This reports whether the physical pointer scrolling direction matches the logical axis direction.
*
* This happens:
*   - when the compositor provides relative-direction metadata for a pointer axis
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   direction  - wl_pointer_axis_relative_direction value for the axis
*/
static void pointer_axis_relative_direction(void* data, struct wl_pointer* wl_pointer, u32 axis, u32 direction) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis relative direction: axis=%" PRIu32 ", direction=%" PRIu32, axis, direction);
}

/**
* This advertises the keymap used by the compositor for a keyboard.
*
* This happens:
*   - after binding a wl_keyboard
*   - whenever the compositor updates the keymap
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   format      - wl_keyboard_keymap_format value
*   fd          - file descriptor for the keymap payload
*   size        - size in bytes of the keymap payload
*/
static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard, u32 format, i32 fd, u32 size) {
    (void)wl_keyboard;

    wlclient_input_device* idev = data;
    destroy_input_device_xkb(idev);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        WLCLIENT_LOG_WARN("Unsupported keyboard keymap format: %" PRIu32, format);
        if (fd >= 0) close(fd);
        return;
    }

    if (fd < 0 || size == 0) {
        WLCLIENT_LOG_WARN("Invalid keyboard keymap payload");
        if (fd >= 0) close(fd);
        return;
    }

    char* keymap_data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (keymap_data == MAP_FAILED) {
        WLCLIENT_LOG_WARN("Failed to map keyboard keymap: %s", strerror(errno));
        close(fd);
        return;
    }

    idev->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!idev->xkb_context) {
        WLCLIENT_LOG_WARN("Failed to create XKB context");
        munmap(keymap_data, size);
        close(fd);
        return;
    }

    idev->xkb_keymap = xkb_keymap_new_from_string(
        idev->xkb_context,
        keymap_data,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );

    munmap(keymap_data, size);
    close(fd);

    if (!idev->xkb_keymap) {
        WLCLIENT_LOG_WARN("Failed to create XKB keymap");
        destroy_input_device_xkb(idev);
        return;
    }

    idev->xkb_state = xkb_state_new(idev->xkb_keymap);
    if (!idev->xkb_state) {
        WLCLIENT_LOG_WARN("Failed to create XKB state");
        destroy_input_device_xkb(idev);
        return;
    }

    const char* locale = setlocale(LC_CTYPE, NULL);
    if (!locale) locale = "C";

    idev->xkb_compose_table = xkb_compose_table_new_from_locale(
        idev->xkb_context,
        locale,
        XKB_COMPOSE_COMPILE_NO_FLAGS
    );
    if (idev->xkb_compose_table) {
        idev->xkb_compose_state = xkb_compose_state_new(
            idev->xkb_compose_table,
            XKB_COMPOSE_STATE_NO_FLAGS
        );
        if (!idev->xkb_compose_state) {
            WLCLIENT_LOG_WARN("Failed to create XKB compose state");
        }
    }
    else {
        WLCLIENT_LOG_WARN("Failed to create XKB compose table for locale '%s'", locale);
    }

    WLCLIENT_LOG_TRACE("Keyboard keymap: format=%" PRIu32, format);
}

/**
* This notifies the client that keyboard focus entered one of its surfaces.
*
* This happens:
*   - when the compositor grants keyboard focus to the client surface
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the enter event
*   surface     - the wl_surface that received keyboard focus
*   keys        - array of keycodes already pressed when focus was entered
*/
static void keyboard_enter(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface, struct wl_array* keys) {
    (void)wl_keyboard;
    (void)keys;

    wlclient_input_device* idev = data;
    idev->keyboard_target_surface = surface;

    wlclient_window* window = wl_surface_get_user_data(surface);
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    if (surface == wdata->surface && wdata->keyboard_focus_handler) {
        wdata->keyboard_focus_handler(window, true);
    }

    WLCLIENT_LOG_TRACE("Keyboard enter: serial=%" PRIu32, serial);
}

/**
* This notifies the client that keyboard focus left one of its surfaces.
*
* This happens:
*   - when the compositor removes keyboard focus from the client surface
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the leave event
*   surface     - the wl_surface that lost keyboard focus
*/
static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface) {
    (void)wl_keyboard;

    wlclient_input_device* idev = data;
    wlclient_window* window = wl_surface_get_user_data(surface);
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    if (surface == wdata->surface && wdata->keyboard_focus_handler) {
        wdata->keyboard_focus_handler(window, false);
    }

    if (idev->keyboard_target_surface == surface) {
        idev->keyboard_target_surface = NULL;
    }

    WLCLIENT_LOG_TRACE("Keyboard leave: serial=%" PRIu32, serial);
}

/**
* This reports a keyboard key press or release.
*
* This happens:
*   - whenever a focused keyboard key changes state
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the key event
*   time        - compositor timestamp in milliseconds
*   key         - hardware keycode reported by the compositor
*   state       - wl_keyboard_key_state value for press or release
*/
static void keyboard_key(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 time, u32 key, u32 state) {
    (void)wl_keyboard;

    wlclient_input_device* idev = data;
    u32 keysym = 0;
    u32 modifiers = keyboard_translate_modifiers(idev);

    if (idev->xkb_state) {
        keysym = xkb_state_key_get_one_sym(idev->xkb_state, key + 8);
    }

    if (idev->keyboard_target_surface) {
        wlclient_window* window = wl_surface_get_user_data(idev->keyboard_target_surface);
        wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
        if (idev->keyboard_target_surface == wdata->surface) {
            const bool is_pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;

            if (wdata->keyboard_key_handler) {
                wdata->keyboard_key_handler(window, key, keysym, is_pressed, modifiers);
            }

            if (is_pressed) {
                keyboard_dispatch_text(idev, wdata, window, key, keysym);
            }
        }
    }

    WLCLIENT_LOG_TRACE(
        "Keyboard key: serial=%" PRIu32 ", time=%" PRIu32 ", keycode=%" PRIu32 ", keysym=%" PRIu32 ", state=%" PRIu32 ", modifiers=%" PRIu32,
        serial, time, key, keysym, state, modifiers
    );
}

/**
* This reports the compositor's current keyboard modifier state.
*
* This happens:
*   - whenever depressed, latched, locked, or layout-group state changes
*
* Parameters:
*   data           - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard    - the wl_keyboard instance
*   serial         - serial identifying the modifiers event
*   mods_depressed - bitmask of depressed modifiers
*   mods_latched   - bitmask of latched modifiers
*   mods_locked    - bitmask of locked modifiers
*   group          - active keyboard layout group
*/
static void keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group) {
    (void)wl_keyboard;

    wlclient_input_device* idev = data;
    u32 modifiers = 0;
    if (idev->xkb_state) {
        xkb_state_update_mask(idev->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
        modifiers = keyboard_translate_modifiers(idev);
    }

    if (idev->keyboard_target_surface) {
        wlclient_window* window = wl_surface_get_user_data(idev->keyboard_target_surface);
        wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
        if (idev->keyboard_target_surface == wdata->surface && wdata->keyboard_modifiers_handler) {
            wdata->keyboard_modifiers_handler(window, modifiers);
        }
    }

    WLCLIENT_LOG_TRACE(
        "Keyboard modifiers: serial=%" PRIu32 ", raw_depressed=%" PRIu32 ", raw_latched=%" PRIu32 ", raw_locked=%" PRIu32 ", group=%" PRIu32 ", wlclient_modifiers=%" PRIu32,
        serial, mods_depressed, mods_latched, mods_locked, group, modifiers
    );
}

/**
* This reports the compositor's preferred keyboard repeat configuration.
*
* This happens:
*   - after binding a wl_keyboard
*   - whenever the compositor updates key repeat settings
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   rate        - repeat rate in keys per second, or zero if disabled
*   delay       - repeat delay in milliseconds
*/
static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard, i32 rate, i32 delay) {
    (void)wl_keyboard;

    wlclient_input_device* idev = data;
    if (idev->keyboard_target_surface) {
        wlclient_window* window = wl_surface_get_user_data(idev->keyboard_target_surface);
        wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
        if (idev->keyboard_target_surface == wdata->surface && wdata->keyboard_repeat_info_handler) {
            wdata->keyboard_repeat_info_handler(window, rate, delay);
        }
    }

    WLCLIENT_LOG_TRACE("Keyboard repeat info: rate=%" PRIi32 ", delay=%" PRIi32, rate, delay);
}
