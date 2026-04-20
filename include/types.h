#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Basic types */
typedef int8_t          i8;
typedef int16_t         i16;
typedef int32_t         i32;
typedef int64_t         i64;
typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef uint64_t        u64;
typedef float           f32;
typedef double          f64;
typedef unsigned char   uchar;
typedef signed char     schar;
typedef u64             usize;
typedef i64             isize;
typedef u32             rune; /* Runes represent a single UTF-32 encoded character. */

/* Storage sizes */
#define WLCLIENT_BYTE      UINT64_C(1)
#define WLCLIENT_KILOBYTE  (UINT64_C(1024) * WLCLIENT_BYTE)
#define WLCLIENT_MEGABYTE  (UINT64_C(1024) * WLCLIENT_KILOBYTE)
#define WLCLIENT_GIGABYTE  (UINT64_C(1024) * WLCLIENT_MEGABYTE)
#define WLCLIENT_TERABYTE  (UINT64_C(1024) * WLCLIENT_GIGABYTE)

/* Duration constants in ns */
#define WLCLIENT_NANOSECOND   UINT64_C(1)                               /*                 1ns */
#define WLCLIENT_MICROSECOND  (UINT64_C(1000) * WLCLIENT_NANOSECOND)    /*             1_000ns */
#define WLCLIENT_MILLISECOND  (UINT64_C(1000) * WLCLIENT_MICROSECOND)   /*         1_000_000ns */
#define WLCLIENT_SECOND       (UINT64_C(1000) * WLCLIENT_MILLISECOND)   /*     1_000_000_000ns */
#define WLCLIENT_MINUTE       (UINT64_C(60)   * WLCLIENT_SECOND)        /*    60_000_000_000ns */
#define WLCLIENT_HOUR         (UINT64_C(60)   * WLCLIENT_MINUTE)        /* 3_600_000_000_000ns */

#define WLCLIENT_WINDOWS_COUNT 5
#define WLCLIENT_BYTES_PER_PIXEL 4
#define WLCLIENT_FRAMEBUFFERS_COUNT 2
#define WLCLIENT_MAX_INPUT_DEVICES 5

typedef enum wlclient_error_code {
    WLCLIENT_ERROR_OK,

    WLCLIENT_ERROR_INIT_FAILED,
    WLCLIENT_ERROR_EVENT_POLL_FAILED,
    WLCLIENT_ERROR_EVENT_POLL_TIMEOUT,
    WLCLIENT_ERROR_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_WINDOW_TOGGLE_DECOR_FAILED,

    WLCLIENT_ERROR_EGL_INIT_FAILED,
    WLCLIENT_ERROR_EGL_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_SET_CONTEXT_FAILED,
    WLCLIENT_ERROR_EGL_SWAP_BUFFERS_FAILED,
    WLCLIENT_ERROR_EGL_SET_SWAP_INTERVAL_FAILED,

    WLCLIENT_ERROR_SENTINEL
} wlclient_error_code;

typedef void (*wlclient_close_handler)(void);

typedef struct wlclient_allocator {
    void* (*alloc)(usize size);
    void (*free)(void* addr);
    char* (*strdup)(const char*);
} wlclient_allocator;

typedef struct wlclient_color {
    u8 r, g, b, a;
} wlclient_color;

typedef struct wlclient_window_decoration_config {
    u32 decor_logical_height; // The height of the window decoration. 0 means no decoration.
    u32 edge_logical_thickness; // The thickness for the edge decorations. 0 means no edges.
    wlclient_color edge_color; // Colors for the edges (i.e. window border colors)
    wlclient_color decor_color; // Colors for the decoration
} wlclient_window_decoration_config;

const static wlclient_window_decoration_config WLCLIENT_NO_DECORATION_CONFIG = {
    .edge_logical_thickness = 0,
    .decor_logical_height = 0,
    .edge_color = {0},
    .decor_color = {0}
};

typedef struct wlclient_input_device {
    bool used;
    u32 seat_id;
    u32 capabilities;
    struct wl_seat* seat;
    u32 seat_version;
    char* seat_name;
    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;
} wlclient_input_device;

typedef enum wlclient_edge {
    WLCLIENT_EDGE_TOP,
    WLCLIENT_EDGE_BOTTOM,
    WLCLIENT_EDGE_LEFT,
    WLCLIENT_EDGE_RIGHT,
    WLCLIENT_EDGE_COUNT,
} wlclient_edge;

typedef struct wlclient_surface_node {
    // The child surface attached to the main window surface.
    struct wl_surface* child_surface;
    // Subsurface links the child_surface to the main window surface.
    struct wl_subsurface* subsurface;
    // Shared-memory pool backing all buffers for this surface node.
    struct wl_shm_pool* shm_pool;
    // Buffers attached to child_surface.
    struct wl_buffer* buffers[WLCLIENT_FRAMEBUFFERS_COUNT];
    // Decoration dimensions in pixels (logical size * buffer_scale).
    u32 pixel_width;
    u32 pixel_height;
    // Indicates when the corresponding buffer has been released by the compositor and may be reused.
    bool ready_states[WLCLIENT_FRAMEBUFFERS_COUNT];
    // Anonymous file descriptor backing shm_pool.
    i32 anon_file_fd;
    // A flat pixel storage for all buffers in shm_pool. Memory mapped from the anonymous file.
    u8* pixel_data;
} wlclient_surface_node;

typedef struct wlclient_window {
    i32 id;
} wlclient_window;

typedef struct wlclient_window_data {
    bool used;

    // This packet is assembled during configuration changes and it is used in xdg_surface_configure
    struct {
        u32 window_logical_width, window_logical_height;
        u32 window_max_logical_width, window_max_logical_height;
    } toplevel_config_in_flight_packet;

    // Logical window geometry in surface coordinates, including client-side decorations and edges.
    u32 window_logical_width, window_logical_height;
    // Logical content area, excluding client-side decorations and edges.
    u32 content_logical_width, content_logical_height;
    // Compositor-suggested maximum logical size.
    u32 window_max_logical_width, window_max_logical_height;
    // Pixel dimensions of the render target (content logical size * buffer_scale). Excludes decorations and edges.
    u32 framebuffer_pixel_width, framebuffer_pixel_height;
    // Scale factor reported by the compositor. May be fractional.
    // Multiply logical sizes by this value to obtain pixel sizes.
    f32 scale_factor;

    // Core main wayland surface — the drawable area that pixel buffers attach to.
    struct wl_surface* surface;
    // XDG surface role — adds configure/ack lifecycle to the raw surface.
    struct xdg_surface* xdg_surface;
    // XDG toplevel role — makes the surface a desktop window with title, close, resize, etc.
    struct xdg_toplevel* xdg_toplevel;

    // Should the frame around the content be visible.
    bool csd_hidden;

    // Logical decoration height in surface coordinates. Immutable after window creation.
    u32 decor_logical_height;
    wlclient_color decor_color;
    // The node for the client side decoration. Positioned above the main surface to render the title bar.
    wlclient_surface_node decor_node;

    // Logical border thickness for all edges. Immutable after window creation.
    u32 edge_logical_thickness;
    wlclient_color edge_color;
    // These are the surface nodes that are used to provide hit regions for interactive window resizing.
    // These are rendered around the window as boarders.
    wlclient_surface_node edge_nodes[WLCLIENT_EDGE_COUNT];

    // User hooks
    wlclient_close_handler close_handler;
} wlclient_window_data;

typedef struct wlclient_global_state {
    wlclient_allocator allocator;

    // The object that represents a client connection to a Wayland compositor.
    struct wl_display* display;
    // The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
    struct wl_registry* registry;
    // The XDG WM base object is the entry point for creating desktop-style windows in Wayland.
    struct xdg_wm_base* xdgWmBase;

    // The compositor is the factory interface for creating surfaces and regions.
    struct wl_compositor* compositor;
    // The object exposing sub-surface compositing capabilities. This is needed for the decoration subsurface.
    struct wl_subcompositor* subcompositor;

    // The singleton global object that provides support for shared memory.
    struct wl_shm* shm;
    // The preferred pixel format for software rendering.
    i32 preferred_pixel_format;

    // State for all the open windows.
    wlclient_window_data windows[WLCLIENT_WINDOWS_COUNT];

    // State for input devices.
    struct wlclient_input_device input_devices[WLCLIENT_MAX_INPUT_DEVICES];
    i32 input_devices_count;

    // Backend hooks.
    void (*backend_shutdown)(void);
    void (*backend_destroy_window)(const wlclient_window* window);
    void (*backend_resize_window)(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height);
} wlclient_global_state;
