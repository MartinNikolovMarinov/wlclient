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
#define WLCLIENT_FRAME_BUFFERS_COUNT 2
#define WLCLIENT_MAX_INPUT_DEVICES 5

typedef enum wlclient_error_code {
    WLCLIENT_ERROR_OK,

    WLCLIENT_ERROR_INIT_FAILED,
    WLCLIENT_ERROR_EVENT_DISPATCH_FAILED,
    WLCLIENT_ERROR_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_INIT_FAILED,
    WLCLIENT_ERROR_EGL_WINDOW_CREATE_FAILED,
    WLCLIENT_ERROR_EGL_SET_CONTEXT_FAILED,
    WLCLIENT_ERROR_EGL_SWAP_BUFFERS_FAILED,
    WLCLIENT_ERROR_EGL_SET_SWAP_INTERVAL_FAILED,

    WLCLIENT_ERROR_SENTINEL
} wlclient_error_code;

typedef struct wlclient_allocator {
    void* (*alloc)(usize size);
    char* (*strdup)(const char*);
} wlclient_allocator;

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

typedef struct wlclient_window {
    i32 id;
} wlclient_window;

typedef struct wlclient_window_data {
    bool used;
} wlclient_window_data;

typedef struct wlclient_global_state {
    wlclient_allocator allocator;

    // The object thar represents a client connection to a Wayland compositor.
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
    i32 preffered_pixel_format;

    // State for all the open windows.
    wlclient_window_data windows[WLCLIENT_WINDOWS_COUNT];

    // State for input devices.
    struct wlclient_input_device input_devices[WLCLIENT_MAX_INPUT_DEVICES];
    i32 input_devices_count;

    // Backend hooks.
    void (*backend_shutdown)(void);
    void (*backend_destroy_window)(const wlclient_window* window);
    void (*backend_resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height);
} wlclient_global_state;
