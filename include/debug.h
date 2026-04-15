#pragma once

#include "API.h"
#include "compiler.h"
#include "types.h"

#include <stdlib.h> // IWYU pragma: keep

struct wl_display;

typedef enum wlclient_log_level {
    WLCLIENT_LOG_LEVEL_TRACE = 0,
    WLCLIENT_LOG_LEVEL_DEBUG,
    WLCLIENT_LOG_LEVEL_INFO,
    WLCLIENT_LOG_LEVEL_WARN,
    WLCLIENT_LOG_LEVEL_ERROR,
    WLCLIENT_LOG_LEVEL_FATAL,

    WLCLIENT_LOG_LEVEL_SENTINEL
} wlclient_log_level;

WLCLIENT_API_EXPORT void wlclient_log_set_level(wlclient_log_level level);
WLCLIENT_API_EXPORT wlclient_log_level wlclient_log_get_level(void);
WLCLIENT_API_EXPORT void wlclient_log_set_use_ansi(i32 use_ansi);
WLCLIENT_API_EXPORT i32 wlclient_log_get_use_ansi(void);

WLCLIENT_API_INTERNAL void _wlclient_log_message(
    wlclient_log_level level,
    const char* file_name,
    const char* format,
    ...
) PRINTF_LIKE(3, 4);

#define WLCLIENT_LOG_TRACE(format, ...) _wlclient_log_message(WLCLIENT_LOG_LEVEL_TRACE, __func__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_DEBUG(format, ...) _wlclient_log_message(WLCLIENT_LOG_LEVEL_DEBUG, __func__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_INFO(format, ...)  _wlclient_log_message(WLCLIENT_LOG_LEVEL_INFO,  __func__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_WARN(format, ...)  _wlclient_log_message(WLCLIENT_LOG_LEVEL_WARN,  __func__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_ERR(format, ...)   _wlclient_log_message(WLCLIENT_LOG_LEVEL_ERROR, __func__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_FATAL(format, ...) _wlclient_log_message(WLCLIENT_LOG_LEVEL_FATAL, __func__, format, ##__VA_ARGS__)

WLCLIENT_API_INTERNAL void _wlclient_report_wayland_fatal(
    struct wl_display* wl_display,
    const char* expr_str,
    const char* file_name,
    i32 line_number
);

WLCLIENT_API_INTERNAL void _wlclient_report_fatal(
    const char* expr_str,
    const char* file_name,
    i32 line_number
);

#if defined(WLCLIENT_ASSERT_ENABLED) && WLCLIENT_ASSERT_ENABLED == 1

#define WLCLIENT_ASSERT(expr, ...)                                                \
do {                                                                              \
    if (!(expr)) {                                                                \
        _wlclient_log_message(WLCLIENT_LOG_LEVEL_FATAL, __func__, ##__VA_ARGS__); \
        _wlclient_report_fatal(#expr, __FILE__, __LINE__);                        \
        abort();                                                                  \
    }                                                                             \
}                                                                                 \
while(0)

#else

#define WLCLIENT_ASSERT(...)

#endif

#define WLCLIENT_PANIC(expr, ...)                                                 \
do {                                                                              \
    if (!(expr)) {                                                                \
        _wlclient_log_message(WLCLIENT_LOG_LEVEL_FATAL, __func__, ##__VA_ARGS__); \
        _wlclient_report_fatal(#expr, __FILE__, __LINE__);                        \
        abort();                                                                  \
    }                                                                             \
}                                                                                 \
while(0)
