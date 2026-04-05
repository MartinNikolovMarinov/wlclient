#pragma once

#include "API.h"
#include "compiler.h"
#include "macro_magic.h"
#include "types.h"

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

WLCLIENT_API_EXPORT void wlclient_log_message(
    wlclient_log_level level,
    const char* file_name,
    i32 line_number,
    const char* format,
    ...
) PRINTF_LIKE(4, 5);

#define WLCLIENT_LOG_TRACE(format, ...) wlclient_log_message(WLCLIENT_LOG_LEVEL_TRACE, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_DEBUG(format, ...) wlclient_log_message(WLCLIENT_LOG_LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_INFO(format, ...)  wlclient_log_message(WLCLIENT_LOG_LEVEL_INFO,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_WARN(format, ...)  wlclient_log_message(WLCLIENT_LOG_LEVEL_WARN,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_ERR(format, ...)   wlclient_log_message(WLCLIENT_LOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define WLCLIENT_LOG_FATAL(format, ...) wlclient_log_message(WLCLIENT_LOG_LEVEL_FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define WLCLIENT_RETURN_ERRNO_IF(expression, error_code)            \
    do {                                                            \
        if (expression) {                                           \
            i32 WLCLIENT_CONCAT(saved_errno_, __LINE__) = errno;    \
            WLCLIENT_LOG_ERR(                                       \
                WLCLIENT_STRINGIFY(expression) " failed: %s",       \
                strerror(WLCLIENT_CONCAT(saved_errno_, __LINE__))   \
            );                                                      \
            return (error_code);                                    \
        }                                                           \
    } while (0)
