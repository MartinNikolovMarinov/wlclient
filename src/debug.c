#include "ansi_codes.h"
#include "debug.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client-core.h>

#define WLCLIENT_USE_ANSI // TODO: make a compile option

#ifdef WLCLIENT_USE_ANSI
static const bool g_use_ansi = true;
#else
static const bool g_use_ansi = false;
#endif

// TODO: [PERFORMANCE] -- For the final build I should disallow dynamic setting of log levels and instead compile out
//       logging code that is below the max level.
static wlclient_log_level g_log_level = WLCLIENT_LOG_LEVEL_WARN;

static void print_failed(const char* predicate, const char* expr_str, const char* file_name, i32 line_number) {
    fprintf(stderr, "%s expr='%s' at %s:%d", predicate, expr_str, file_name, line_number);
}

static const char* wlclient_log_level_str(wlclient_log_level level) {
    switch (level) {

#ifdef WLCLIENT_USE_ANSI
        case WLCLIENT_LOG_LEVEL_TRACE:
            return ANSI_BOLD_START() ANSI_BRIGHT_GREEN_START() "[TRACE]" ANSI_RESET();
        case WLCLIENT_LOG_LEVEL_DEBUG:
            return ANSI_BOLD_START() "[DEBUG]" ANSI_RESET();
        case WLCLIENT_LOG_LEVEL_INFO:
             return ANSI_BOLD_START() ANSI_BLUE_START() "[INFO]" ANSI_RESET();
        case WLCLIENT_LOG_LEVEL_WARN:
             return ANSI_BOLD_START() ANSI_YELLOW_START() "[WARN]" ANSI_RESET();
        case WLCLIENT_LOG_LEVEL_ERROR:
            return ANSI_BOLD_START() ANSI_RED_START() "[ERROR]" ANSI_RESET();
        case WLCLIENT_LOG_LEVEL_FATAL:
            return ANSI_BOLD_START() ANSI_BRIGHT_WHITE_START() ANSI_BACKGROUND_RED_START() "[FATAL]" ANSI_RESET();

#else
        case WLCLIENT_LOG_LEVEL_TRACE: return "[TRACE]";
        case WLCLIENT_LOG_LEVEL_DEBUG: return "[DEBUG]";
        case WLCLIENT_LOG_LEVEL_INFO:  return "[INFO]";
        case WLCLIENT_LOG_LEVEL_WARN:  return "[WARN]";
        case WLCLIENT_LOG_LEVEL_ERROR: return "[ERROR]";
        case WLCLIENT_LOG_LEVEL_FATAL: return "[FATAL]";
#endif

        case WLCLIENT_LOG_LEVEL_SENTINEL:
        default:
            return "UNKNOWN";
    }
}

void wlclient_log_set_level(wlclient_log_level level) {
    if (level >= WLCLIENT_LOG_LEVEL_SENTINEL) {
        return;
    }

    g_log_level = level;
}

wlclient_log_level wlclient_log_get_level(void) {
    return g_log_level;
}

void _wlclient_log_message(
    wlclient_log_level level,
    const char* function_name,
    const char* format,
    ...
) {
    if (level < g_log_level) {
        return;
    }

    fprintf(stdout, "%s %s() -- ", wlclient_log_level_str(level), function_name);

    // Print message:
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);

    fputc('\n', stdout);
}

void _wlclient_report_wayland_fatal(
    struct wl_display* wl_display,
    const char* expr_str,
    const char* file_name,
    i32 line_number
) {
    i32 err = 0;
    u32 protocol_err_id = 0;
    u32 protocol_err_code = 0;
    const struct wl_interface *iface = NULL;

    err = wl_display_get_error(wl_display);
    if (err) {
        protocol_err_code = wl_display_get_protocol_error(wl_display, &iface, &protocol_err_id);
    }

    print_failed(wlclient_log_level_str(WLCLIENT_LOG_LEVEL_ERROR), expr_str, file_name, line_number);

    if (err) {
        fprintf(stderr, " | wayland: errno=%"PRIi32 " (%s)", err, strerror(err));

        if (iface) {
            fprintf(
                stderr,
                ", protocol: iface=%s id=%"PRIu32" code=%"PRIu32,
                iface->name, protocol_err_id, protocol_err_code
            );
        }
        else {
            fprintf(stderr, ", protocol: <none>");
        }
    }

    fputc('\n', stderr);
}

void _wlclient_report_assertion(
    const char* expr_str,
    const char* file_name,
    i32 line_number
) {
    const char* failed_str = g_use_ansi
        ? ANSI_BOLD(ANSI_BRIGHT_WHITE(ANSI_BACKGROUND_RED("[ASSERTION]")))
        : "[ASSERTION]";
    print_failed(failed_str, expr_str, file_name, line_number);
    fputc('\n', stderr);
}

void _wlclient_report_error(const char* expr_str, const char* file_name, i32 line_number, const char* format, ...) {
    print_failed(wlclient_log_level_str(WLCLIENT_LOG_LEVEL_ERROR), expr_str, file_name, line_number);

    if (format) {
        fprintf(stderr, " -- ");
        // Print message:
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }

    fputc('\n', stderr);
}

