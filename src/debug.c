#include "ansi_codes.h"
#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client-core.h>

static wlclient_log_level g_log_level = WLCLIENT_LOG_LEVEL_WARN;
static i32 g_log_use_ansi = 1;

static const char* wlclient_log_level_name(wlclient_log_level level) {
    switch (level) {
        case WLCLIENT_LOG_LEVEL_TRACE: return "TRACE";
        case WLCLIENT_LOG_LEVEL_DEBUG: return "DEBUG";
        case WLCLIENT_LOG_LEVEL_INFO:  return "INFO";
        case WLCLIENT_LOG_LEVEL_WARN:  return "WARN";
        case WLCLIENT_LOG_LEVEL_ERROR: return "ERROR";
        case WLCLIENT_LOG_LEVEL_FATAL: return "FATAL";

        case WLCLIENT_LOG_LEVEL_SENTINEL:
        default:
            return "UNKNOWN";
    }
}

static const char* wlclient_log_level_ansi_start(wlclient_log_level level) {
    switch (level) {
        case WLCLIENT_LOG_LEVEL_TRACE: return ANSI_BOLD_START() ANSI_BRIGHT_GREEN_START();
        case WLCLIENT_LOG_LEVEL_DEBUG: return ANSI_BOLD_START();
        case WLCLIENT_LOG_LEVEL_INFO:  return ANSI_BOLD_START() ANSI_BLUE_START();
        case WLCLIENT_LOG_LEVEL_WARN:  return ANSI_BOLD_START() ANSI_YELLOW_START();
        case WLCLIENT_LOG_LEVEL_ERROR: return ANSI_BOLD_START() ANSI_RED_START();
        case WLCLIENT_LOG_LEVEL_FATAL: return ANSI_BOLD_START() ANSI_BRIGHT_WHITE_START() ANSI_BACKGROUND_RED_START();

        case WLCLIENT_LOG_LEVEL_SENTINEL:
        default:
            return "";
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

void wlclient_log_set_use_ansi(i32 use_ansi) {
    g_log_use_ansi = use_ansi != 0;
}

i32 wlclient_log_get_use_ansi(void) {
    return g_log_use_ansi;
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

    if (g_log_use_ansi) {
        fprintf(
            stdout,
            "%s[%s]%s %s() -- ",
            wlclient_log_level_ansi_start(level),
            wlclient_log_level_name(level),
            ANSI_RESET(),
            function_name
        );
    }
    else {
        fprintf(stdout, "[%s] %s() -- ", wlclient_log_level_name(level), function_name);
    }

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

    const char* failed_str = g_log_use_ansi
        ? ANSI_BOLD(ANSI_BRIGHT_WHITE(ANSI_BACKGROUND_RED("[FAILED]")))
        : "[FAILED]";
    fprintf(stderr, "%s expr='%s' at %s:%d", failed_str, expr_str, file_name, line_number);

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

void _wlclient_report_fatal(
    const char* expr_str,
    const char* file_name,
    i32 line_number
) {
    const char* failed_str = g_log_use_ansi
        ? ANSI_BOLD(ANSI_BRIGHT_WHITE(ANSI_BACKGROUND_RED("[ASSERTION]")))
        : "[ASSERTION]";
    fprintf(stderr, "%s expr='%s' at %s:%d\n", failed_str, expr_str, file_name, line_number);
}
