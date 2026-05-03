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

#define WLCLIENT_MOD_SHIFT     (1u << 0)
#define WLCLIENT_MOD_CONTROL   (1u << 1)
#define WLCLIENT_MOD_ALT       (1u << 2)
#define WLCLIENT_MOD_SUPER     (1u << 3)
#define WLCLIENT_MOD_CAPS_LOCK (1u << 4)
#define WLCLIENT_MOD_NUM_LOCK  (1u << 5)

#define WLCLIENT_MOD_HAS(modifiers, flag) (((modifiers) & (flag)) != 0)

typedef enum wlclient_error_code {
    WLCLIENT_ERROR_OK,

    WLCLIENT_ERROR_INIT_FAILED,
    WLCLIENT_ERROR_EVENT_POLL_FAILED,
    WLCLIENT_ERROR_EVENT_POLL_TIMEOUT,
    WLCLIENT_ERROR_WINDOW_CREATE_FAILED,

    WLCLIENT_ERROR_EGL_INIT_FAILED,
    WLCLIENT_ERROR_EGL_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_SET_CONTEXT_FAILED,
    WLCLIENT_ERROR_EGL_SWAP_BUFFERS_FAILED,
    WLCLIENT_ERROR_EGL_SET_SWAP_INTERVAL_FAILED,

    WLCLIENT_ERROR_SENTINEL
} wlclient_error_code;

struct wlclient_window;

// Windowing events:
typedef void (*wlclient_close_handler)(struct wlclient_window* window);
typedef void (*wlclient_size_change_handler)(struct wlclient_window* window, u32 width, u32 height);
typedef void (*wlclient_framebuffer_change_handler)(struct wlclient_window* window, u32 width, u32 height);
typedef void (*wlclient_scale_factor_change_handler)(struct wlclient_window* window, f32 factor);
typedef void (*wlclient_suspended_handler)(struct wlclient_window* window, bool is_suspended);
typedef void (*wlclient_fullscreen_handler)(struct wlclient_window* window, bool is_fullscreen);

// Mouse input events:
typedef void (*wlclient_mouse_focus_handler)(struct wlclient_window* window, bool hasMouseFocus);
typedef void (*wlclient_mouse_move_handler)(struct wlclient_window* window, f64 x, f64 y);
typedef void (*wlclient_mouse_press_handler)(struct wlclient_window* window, u32 button, bool isPressed, f64 x, f64 y);

// Keyboard input events:
typedef void (*wlclient_keyboard_focus_handler)(struct wlclient_window* window, bool hasKeyboardFocus);
typedef void (*wlclient_keyboard_key_handler)(
    struct wlclient_window* window,
    u32 keycode,
    u32 keysym,
    bool isPressed,
    u32 modifiers
);
typedef void (*wlclient_keyboard_text_handler)(struct wlclient_window* window, const char* utf8, usize len);
typedef void (*wlclient_keyboard_modifiers_handler)(struct wlclient_window* window, u32 modifiers);
typedef void (*wlclient_keyboard_repeat_info_handler)(struct wlclient_window* window, i32 rate, i32 delay_ms);

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

static const wlclient_window_decoration_config WLCLIENT_NO_DECORATION_CONFIG = {
    .edge_logical_thickness = 0,
    .decor_logical_height = 0,
    .edge_color = {0},
    .decor_color = {0}
};

typedef enum wlclient_surface_type {
    ST_UNKNOWN,
    ST_MAIN,
    ST_DECOR,
    ST_TOP_EDGE,
    ST_RIGHT_EDGE,
    ST_BOTTOM_EDGE,
    ST_LEFT_EDGE,
    ST_SENTINEL
} wlclient_surface_type;

typedef enum wlclient_toplevel_state {
    TOPLEVEL_STATE_NONE = 0,
    TOPLEVEL_STATE_MAXIMIZED = 1 << 0,
    TOPLEVEL_STATE_FULLSCREEN = 1 << 1,
    TOPLEVEL_STATE_RESIZING = 1 << 2,
    TOPLEVEL_STATE_ACTIVATED = 1 << 3,
    TOPLEVEL_STATE_SUSPENDED = 1 << 4,
    TOPLEVEL_STATE_TILED_LEFT = 1 << 5,
    TOPLEVEL_STATE_TILED_RIGHT = 1 << 6,
    TOPLEVEL_STATE_TILED_TOP = 1 << 7,
    TOPLEVEL_STATE_TILED_BOTTOM = 1 << 8,
} wlclient_toplevel_state;

typedef enum wlclient_toplevel_capabilities {
    TOPLEVEL_CAPABILITIES_NONE = 0,
	TOPLEVEL_CAPABILITIES_CAN_SHOW_WINDOW_MENU = 1 << 0,
	TOPLEVEL_CAPABILITIES_CAN_MAXIMIZE = 1 << 1,
    TOPLEVEL_CAPABILITIES_CAN_MINIMIZE = 1 << 2,
    TOPLEVEL_CAPABILITIES_CAN_FULLSCREEN = 1 << 3,
} wlclient_toplevel_capabilities;

typedef struct wlclient_input_device {
    struct {
        u32 pending_flags;
        // Surface the pointer is currently entered on. Target of motion, button, gained-focus.
        struct wl_surface* target_surface;
        wlclient_surface_type target_surface_type;
        // Surface that lost pointer focus this frame. Separate slot so leave+enter delivered in the same frame can both
        // be dispatched (compositors commonly batch cross-surface transitions).
        struct wl_surface* leave_surface;
        wlclient_surface_type leave_surface_type;
        f64 x, y;
        u32 button;
        // Serial of the most recent button press — required by xdg_toplevel_resize / xdg_toplevel_move.
        u32 button_serial;
    } mouse_in_flight_packet;

    bool used;
    u32 seat_id;
    u32 capabilities;
    struct wl_seat* seat;
    u32 seat_version;
    char* seat_name;
    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;
    // The keyboard target surface helps identify the window for witch a keyboard event is fired.
    struct wl_surface* keyboard_target_surface;

    struct {
        struct xkb_context* context;
        struct xkb_keymap* keymap;
        struct xkb_state* state;
        struct xkb_compose_table* compose_table;
        struct xkb_compose_state* compose_state;
    } xkb;
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

    // This packet is assembled during configuration changes and it is used in xdg_surface_configure.
    struct {
        u32 window_logical_width, window_logical_height;
        wlclient_toplevel_state window_state;
    } toplevel_config_in_flight_packet;

    // Logical window geometry in surface coordinates, including client-side decorations and edges.
    u32 window_logical_width, window_logical_height;
    // Logical content area, excluding client-side decorations and edges.
    u32 content_logical_width, content_logical_height;
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
    // Toplevel capabilities
    wlclient_toplevel_capabilities toplevel_capabilities;

    // The window not visible on the screen and the compositor will not be painting it.
    bool is_suspended;
    // The window state is fullscreen.
    bool is_fullscreen;
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
    wlclient_size_change_handler size_change_handler;
    wlclient_framebuffer_change_handler framebuffer_change_handler;
    wlclient_scale_factor_change_handler scale_factor_change_handler;
    wlclient_suspended_handler suspended_handler;
    wlclient_fullscreen_handler fullscreen_handler;
    wlclient_mouse_focus_handler mouse_focus_handler;
    wlclient_mouse_move_handler mouse_move_handler;
    wlclient_mouse_press_handler mouse_press_handler;
    wlclient_keyboard_focus_handler keyboard_focus_handler;
    wlclient_keyboard_key_handler keyboard_key_handler;
    wlclient_keyboard_text_handler keyboard_text_handler;
    wlclient_keyboard_modifiers_handler keyboard_modifiers_handler;
    wlclient_keyboard_repeat_info_handler keyboard_repeat_info_handler;
} wlclient_window_data;

typedef struct wlclient_global_state {
    // All dynamic memory allocations performed by this library go through this interface. This excludes allocations
    // made internally by the standard library or external dependencies.
    wlclient_allocator allocator;

    // The object that represents a client connection to a Wayland compositor.
    struct wl_display* display;
    // The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
    struct wl_registry* registry;
    // The XDG WM base object is the entry point for creating desktop-style windows in Wayland.
    struct xdg_wm_base* xdgWmBase;

    // The compositor is the factory interface for creating surfaces and regions.
    struct wl_compositor* compositor;
    // The object exposing sub-surface compositing capabilities. This is needed for the decoration subsurfaces.
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
    void (*backend_resize_framebuffer)(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height);
    void (*backend_scale_change)(const wlclient_window* window, f32 factor);
} wlclient_global_state;
