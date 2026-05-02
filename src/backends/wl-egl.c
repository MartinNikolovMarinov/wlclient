#define _GNU_SOURCE

#include "wl-egl.h"

#include "debug.h"
#include "types.h"
#include "wl-client.h"
#include "wl-utils.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <EGL/egl.h>
#include <wayland-egl.h>

#include "wayland-client-protocol.h"
#include "wayland-client-core.h"

#define ENSURE_OR_GOTO_ERR(expr)                                                                 \
do {                                                                                             \
    if (!(expr)) {                                                                               \
        EGLint err_code = eglGetError();                                                         \
        _wlclient_report_error(#expr, __FILE__, __LINE__, "EGL error_code = %"PRIi32, err_code); \
        goto error;                                                                              \
    }                                                                                            \
}                                                                                                \
while(0)

static EGLDisplay g_egl_display = EGL_NO_DISPLAY;
static EGLConfig g_egl_config = NULL;
static i32 g_swap_interval = 1;

#define WLCLIENT_EGL_MAX_ATTRIBUTES 128
static EGLint g_config_attribs[WLCLIENT_EGL_MAX_ATTRIBUTES] = {EGL_NONE};
static i32 g_config_attribs_count = 0;
static EGLint g_context_attribs[WLCLIENT_EGL_MAX_ATTRIBUTES] = {EGL_NONE};
static i32 g_context_attribs_count = 0;

typedef struct wlclient_egl_window_data {
    bool used;
    struct wl_egl_window* egl_window;
    EGLContext egl_context;
    EGLSurface egl_surface;
    struct wl_event_queue* frame_queue;
    struct wl_callback* frame_callback;
    bool waiting_for_frame;
} wlclient_egl_window_data;

static wlclient_egl_window_data g_egl_window_data[WLCLIENT_WINDOWS_COUNT] = {0};

//======================================================================================================================
// Hook Declarations
//======================================================================================================================

static void egl_shutdown(void);
static void egl_destroy_window(const wlclient_window* window);
static void egl_resize_window(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height);
static void egl_scale_change_window(const wlclient_window* window, f32 factor);
static void egl_frame_done(void* data, struct wl_callback* callback, u32 callback_data);

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

static void egl_trace_attrs(const EGLint* attribs, const char* label);

static void egl_reset_config_attrs(void);
static void egl_reset_context_attrs(void);

static bool egl_wait_for_frame(wlclient_egl_window_data* egl_wdata);
static bool egl_request_next_frame(wlclient_egl_window_data* egl_wdata, const wlclient_window_data* wdata);

//======================================================================================================================
// PUBLIC
//======================================================================================================================

void wlclient_egl_add_config_attr(EGLint key, EGLint value) {
    WLCLIENT_ASSERT((g_config_attribs_count + 2) < WLCLIENT_EGL_MAX_ATTRIBUTES, "Reached max config attributes");
    g_config_attribs[g_config_attribs_count] = key;
    g_config_attribs[g_config_attribs_count + 1] = value;
    g_config_attribs_count += 2;
}

void wlclient_egl_add_context_attr(EGLint key, EGLint value) {
    WLCLIENT_ASSERT((g_context_attribs_count + 2) < WLCLIENT_EGL_MAX_ATTRIBUTES, "Reached max context attributes");
    g_context_attribs[g_context_attribs_count] = key;
    g_context_attribs[g_context_attribs_count + 1] = value;
    g_context_attribs_count += 2;
}

wlclient_error_code wlclient_egl_set_swap_interval(i32 interval) {
    EGLBoolean ret = eglSwapInterval(g_egl_display, interval);
    ENSURE_OR_GOTO_ERR(ret == EGL_TRUE);
    g_swap_interval = interval;
    return WLCLIENT_ERROR_OK;
error:
    return WLCLIENT_ERROR_EGL_SET_SWAP_INTERVAL_FAILED;
}

wlclient_error_code wlclient_egl_init(EGLenum api) {
    WLCLIENT_LOG_INFO("Initializing EGL backend...");

    i32 config_count;
    EGLint major = 0;
    EGLint minor = 0;

    g_egl_display = eglGetDisplay((EGLNativeDisplayType) _wlclient_get_wl_display());
    ENSURE_OR_GOTO_ERR(g_egl_display != EGL_NO_DISPLAY);

    EGLBoolean init_res = eglInitialize(g_egl_display, &major, &minor);
    ENSURE_OR_GOTO_ERR(init_res == EGL_TRUE);
    EGLBoolean bind_res = eglBindAPI(api);
    ENSURE_OR_GOTO_ERR(bind_res == EGL_TRUE);

    WLCLIENT_LOG_INFO("EGL version %" PRIi32 ".%" PRIi32, major, minor);

    g_config_attribs[g_config_attribs_count] = EGL_NONE; // terminate the config attributes
    egl_trace_attrs(g_config_attribs, "config");
    EGLBoolean choose_cfg_res = eglChooseConfig(g_egl_display, g_config_attribs, &g_egl_config, 1, &config_count);
    ENSURE_OR_GOTO_ERR(choose_cfg_res == EGL_TRUE);
    ENSURE_OR_GOTO_ERR(config_count == 1);

    // Set backend hooks on the wayland window:
    {
        _wlclient_set_backend_shutdown(egl_shutdown);
        _wlclient_set_backend_destroy_window(egl_destroy_window);
        _wlclient_set_backend_resize_framebuffer(egl_resize_window);
        _wlclient_set_backend_scale_change(egl_scale_change_window);
    }

    egl_reset_config_attrs();

    WLCLIENT_LOG_INFO("EGL backend initialization done");
    return WLCLIENT_ERROR_OK;

error:
    egl_reset_config_attrs();
    egl_shutdown();
    return WLCLIENT_ERROR_EGL_INIT_FAILED;
}

wlclient_error_code wlclient_egl_config_window(wlclient_window* window) {
    WLCLIENT_LOG_INFO("Configuring EGL window...");

    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];
    WLCLIENT_ASSERT(!egl_wdata->used, "Egl Window is marked as unused");

    egl_wdata->egl_window = wl_egl_window_create(
        wdata->surface,
        (i32) wdata->framebuffer_pixel_width,
        (i32) wdata->framebuffer_pixel_height
    );
    ENSURE_OR_GOTO_ERR(egl_wdata->egl_window);

    egl_wdata->frame_queue = wl_display_create_queue(_wlclient_get_wl_display());
    ENSURE_OR_GOTO_ERR(egl_wdata->frame_queue);

    g_context_attribs[g_context_attribs_count] = EGL_NONE;
    egl_trace_attrs(g_context_attribs, "context");
    egl_wdata->egl_context = eglCreateContext(g_egl_display, g_egl_config, EGL_NO_CONTEXT, g_context_attribs);
    ENSURE_OR_GOTO_ERR(egl_wdata->egl_context != EGL_NO_CONTEXT);

    egl_wdata->egl_surface = eglCreateWindowSurface(
        g_egl_display,
        g_egl_config,
        (EGLNativeWindowType) egl_wdata->egl_window,
        NULL
    );
    ENSURE_OR_GOTO_ERR(egl_wdata->egl_surface != EGL_NO_SURFACE);

    egl_wdata->used = true;
    egl_reset_context_attrs();

    WLCLIENT_LOG_INFO("EGL window configuration done");
    return WLCLIENT_ERROR_OK;

error:
    egl_reset_context_attrs();
    egl_destroy_window(window);
    return WLCLIENT_ERROR_EGL_WINDOW_CREATE_FAILED;
}

wlclient_error_code wlclient_egl_make_current_context(wlclient_window* window) {
    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];
    WLCLIENT_ASSERT(egl_wdata->used, "EGL Window is marked as unused.");

    EGLBoolean res = eglMakeCurrent(
        g_egl_display,
        egl_wdata->egl_surface,
        egl_wdata->egl_surface,
        egl_wdata->egl_context
    );
    ENSURE_OR_GOTO_ERR(res == EGL_TRUE);

    return WLCLIENT_ERROR_OK;

error:
    return WLCLIENT_ERROR_EGL_SET_CONTEXT_FAILED;
}

wlclient_error_code wlclient_egl_swap_buffers(const wlclient_window* window) {
    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];
    WLCLIENT_ASSERT(egl_wdata->used, "EGL Window is marked as unused.");
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    if (wdata->is_suspended) {
        WLCLIENT_LOG_INFO("Skipping swap buffer - window(id=%d) is suspended", window->id);
        return WLCLIENT_ERROR_OK;
    }

    if (g_swap_interval > 0) {
        if(!egl_wait_for_frame(egl_wdata)) {
            return WLCLIENT_ERROR_OK;
        }
        ENSURE_OR_GOTO_ERR(egl_request_next_frame(egl_wdata, wdata));
    }

    EGLBoolean res = eglSwapBuffers(g_egl_display, egl_wdata->egl_surface);
    ENSURE_OR_GOTO_ERR(res == EGL_TRUE);

    return WLCLIENT_ERROR_OK;

error:
    return WLCLIENT_ERROR_EGL_SWAP_BUFFERS_FAILED;
}

//======================================================================================================================
// Hook Implementations
//======================================================================================================================

static void egl_shutdown(void) {
    WLCLIENT_LOG_DEBUG("Shuttingdown EGL...");

    // Release EGL per-thread state. This should remove the need to use eglMakeCurrent(.. EGL_NO_CONTEXT).
    if (eglReleaseThread() != EGL_TRUE) {
        WLCLIENT_LOG_WARN("Failed to release egl thread");
    }

    if (g_egl_display != EGL_NO_DISPLAY) {
        eglTerminate(g_egl_display);
    }

    g_egl_config = NULL;
    g_egl_display = EGL_NO_DISPLAY;

    egl_reset_config_attrs();
    egl_reset_context_attrs();

    // clear the egl window data array
    memset(g_egl_window_data, 0, WLCLIENT_WINDOWS_COUNT * sizeof(*g_egl_window_data));

    WLCLIENT_LOG_DEBUG("EGL Shutdown");
}

static void egl_destroy_window(const wlclient_window* window) {
    if (!window) return;

    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];

    WLCLIENT_LOG_DEBUG("Destroying EGL window with id=%" PRIi32 "... ", window->id);

    // Surfaces and contexts can't be destroyed if they are current, so make sure current is set to no surface and no context.
    if (egl_wdata->egl_surface != EGL_NO_SURFACE || egl_wdata->egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    if (egl_wdata->egl_surface != EGL_NO_SURFACE) eglDestroySurface(g_egl_display, egl_wdata->egl_surface);
    if (egl_wdata->egl_context != EGL_NO_CONTEXT) eglDestroyContext(g_egl_display, egl_wdata->egl_context);
    if (egl_wdata->egl_window) wl_egl_window_destroy(egl_wdata->egl_window);
    if (egl_wdata->frame_callback) wl_callback_destroy(egl_wdata->frame_callback);
    if (egl_wdata->frame_queue) wl_event_queue_destroy(egl_wdata->frame_queue);

    memset(egl_wdata, 0, sizeof(*egl_wdata));

    WLCLIENT_LOG_DEBUG("Destroyed");
}

static void egl_resize_window(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height) {
    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];
    if (!egl_wdata->used || !egl_wdata->egl_window) return;

    WLCLIENT_LOG_DEBUG(
        "Resizing EGL window with id=%" PRIi32 " to %" PRIi32 "x%" PRIi32,
        window->id,
        framebuffer_width,
        framebuffer_height
    );
    wl_egl_window_resize(egl_wdata->egl_window, (i32)framebuffer_width, (i32)framebuffer_height, 0, 0);
}

static void egl_scale_change_window(const wlclient_window* window, f32 factor) {
    wlclient_egl_window_data* egl_wdata = &g_egl_window_data[window->id];
    if (!egl_wdata->used || !egl_wdata->egl_window) return;

    (void)factor;
    // FIXME: [SCALING] How do I handle this?
}

static void egl_frame_done(void* data, struct wl_callback* callback, u32 callback_data) {
    (void)callback_data;

    wlclient_egl_window_data* egl_wdata = data;
    if (egl_wdata->frame_callback == callback) {
        egl_wdata->frame_callback = NULL;
        egl_wdata->waiting_for_frame = false;
    }
    wl_callback_destroy(callback);
}

//======================================================================================================================
// Helper Implementations
//======================================================================================================================

static void egl_trace_attrs(const EGLint* attribs, const char* label) {
    const EGLint* x = attribs;
    WLCLIENT_LOG_TRACE("%s attributes:", label);
    while (*x != EGL_NONE) {
        WLCLIENT_LOG_TRACE("  [%" PRIi32 "] = %" PRIi32, *(x), *(x + 1));
        x += 2;
    }
}

static void egl_reset_config_attrs(void) {
    memset(g_config_attribs, 0, sizeof(g_config_attribs));
    g_config_attribs[0] = EGL_NONE;
    g_config_attribs_count = 0;
}

static void egl_reset_context_attrs(void) {
    memset(g_context_attribs, 0, sizeof(g_context_attribs));
    g_context_attribs[0] = EGL_NONE;
    g_context_attribs_count = 0;
}

static bool egl_wait_for_frame(wlclient_egl_window_data* egl_wdata) {
    if (!egl_wdata->frame_callback || !egl_wdata->waiting_for_frame) {
        return true;
    }

    static const u64 FRAME_TIMEOUT_NS = 1 * WLCLIENT_SECOND;
    const struct timespec timeout = wlclient_ns_to_timespec(FRAME_TIMEOUT_NS);

    while (egl_wdata->waiting_for_frame) {
        i32 ret = wl_display_dispatch_queue_timeout(_wlclient_get_wl_display(), egl_wdata->frame_queue, &timeout);
        if (ret < 0) {
            WLCLIENT_LOG_WARN("Failed to dispatch EGL frame callback queue");
            return false;
        }
        if (ret == 0 && egl_wdata->waiting_for_frame) {
            WLCLIENT_LOG_WARN("Timed out waiting for EGL frame callback");
            return false;
        }
    }

    return true;
}

static bool egl_request_next_frame(wlclient_egl_window_data* egl_wdata, const wlclient_window_data* wdata) {
    static const struct wl_callback_listener listener = {
        .done = egl_frame_done,
    };

    WLCLIENT_ASSERT(!egl_wdata->frame_callback, "[BUG] EGL frame callback already pending");

    egl_wdata->frame_callback = wl_surface_frame(wdata->surface);
    if (!egl_wdata->frame_callback) {
        WLCLIENT_LOG_WARN("Failed to create EGL frame callback");
        return false;
    }

    wl_proxy_set_queue((struct wl_proxy*)egl_wdata->frame_callback, egl_wdata->frame_queue);

    i32 ret = wl_callback_add_listener(egl_wdata->frame_callback, &listener, egl_wdata);
    if (ret < 0) {
        wl_callback_destroy(egl_wdata->frame_callback);
        egl_wdata->frame_callback = NULL;
        WLCLIENT_LOG_WARN("Failed to add EGL frame callback listener");
        return false;
    }

    egl_wdata->waiting_for_frame = true;
    return true;
}
