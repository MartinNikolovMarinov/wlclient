#include "ansi_codes.h"
#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

static wlclient_log_level g_log_level = WLCLIENT_LOG_LEVEL_INFO;
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

void wlclient_log_message(
    wlclient_log_level level,
    const char* file_name,
    i32 line_number,
    const char* format,
    ...
) {
    va_list args;

    if (level < g_log_level) {
        return;
    }

    if (g_log_use_ansi) {
        fprintf(
            stdout,
            "%s[%s]%s %s:%d -- ",
            wlclient_log_level_ansi_start(level),
            wlclient_log_level_name(level),
            ANSI_RESET(),
            file_name,
            line_number
        );
    }
    else {
        fprintf(stdout, "[%s] %s:%d: ", wlclient_log_level_name(level), file_name, line_number);
    }

    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);

    fputc('\n', stdout);
}
