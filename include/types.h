#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wayland-client-protocol.h>

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

typedef enum wlclient_error_code {
    WLCLIENT_ERROR_OK,

    WLCLIENT_ERROR_INIT_FAILED,
    WLCLIENT_ERROR_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_INIT_FAILED,
    WLCLIENT_ERROR_EGL_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_SET_CONTEXT_FAILED,
    WLCLIENT_ERROR_EGL_SWAP_BUFFERS_FAILED,
    WLCLIENT_ERROR_EGL_SET_SWAP_INTERVAL_FAILED,

    WLCLIENT_ERROR_SENTINEL
} wlclient_error_code;

typedef struct wlclient_window {
    i32 id;
} wlclient_window;

#define WLCLIENT_WINDOWS_COUNT 5
#define WLCLIENT_BYTES_PER_PIXEL 4

typedef enum {
    WLCLIENT_PENDING_NONE              = 0,
    WLCLIENT_PENDING_DECORATION_RESIZE = 1 << 0, // Recreate decoration buffers and update window geometry.
    WLCLIENT_PENDING_BACKEND_RESIZE    = 1 << 1, // Recompute framebuffer size and notify the backend.
} wlclient_pending_flags;

typedef struct wlclient_decoration_config {
    i32 decor_height;
} wlclient_decoration_config;

static const struct wlclient_decoration_config wlclient_no_decoration_config = {0};

typedef struct wlclient_window_data {
    bool used;
    wlclient_pending_flags pending;

    // Logical window geometry in surface coordinates, including client-side decorations.
    i32 logical_width, logical_height;
    // Logical content area, excluding client-side decorations.
    i32 content_logical_width, content_logical_height;
    // Pixel dimensions of the render target (content logical size * buffer_scale).
    i32 framebuffer_pixel_width, framebuffer_pixel_height;
    // Compositor-suggested maximum logical size.
    i32 max_logical_width, max_logical_height;
    // Integer scale factor from the compositor. Multiplied with logical sizes to get pixel sizes.
    i32 buffer_scale;

    // Core wayland surface — the drawable area that pixel buffers attach to.
    struct wl_surface* surface;
    // XDG surface role — adds configure/ack lifecycle to the raw surface.
    struct xdg_surface* xdg_surface;
    // XDG toplevel role — makes the surface a desktop window with title, close, resize, etc.
    struct xdg_toplevel* xdg_toplevel;

    // Decoration subsurface, positioned above the main surface to render the title bar.
    struct wl_surface* decoration_surface;
    // Binds decoration_surface as a child of the main surface.
    struct wl_subsurface* decoration_subsurface;
    // Logical decoration height in surface coordinates. Immutable after window creation.
    i32 decoration_logical_height;
    // Decoration dimensions in pixels (logical size * buffer_scale).
    i32 decoration_pixel_height, decoration_pixel_width;
    // Anonymous file backing the shm pool.
    i32 decoration_anon_file_fd;
    // Pool shared with the compositor.
    struct wl_shm_pool* decoration_shm_pool;
    // Double-buffered wl_buffer handles into the pool.
    struct wl_buffer* decoration_buffers[2];
    // True when the compositor has released the buffer.
    bool decoration_buffer_ready_states[2];
    // Single mmap of the entire pool.
    u8* decoration_pixel_data;
} wlclient_window_data;
