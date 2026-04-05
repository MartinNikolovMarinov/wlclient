#pragma once

#define WLCLIENT_STRINGIFY_IMPL(x) #x
#define WLCLIENT_STRINGIFY(x) WLCLIENT_STRINGIFY_IMPL(x)
#define WLCLIENT_CONCAT_IMPL(a, b) a##b
#define WLCLIENT_CONCAT(a, b) WLCLIENT_CONCAT_IMPL(a, b)

#define WLCLIENT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WLCLIENT_MAX(a, b) ((a) > (b) ? (a) : (b))

#define WLCLIENT_TRY_SYSCALL(expression, error_code)                \
    do {                                                            \
        if (!(expression)) {                                        \
            i32 WLCLIENT_CONCAT(saved_errno_, __LINE__) = errno;    \
            WLCLIENT_LOG_ERR(                                       \
                WLCLIENT_STRINGIFY(expression) " failed: %s",       \
                strerror(WLCLIENT_CONCAT(saved_errno_, __LINE__))   \
            );                                                      \
            return (error_code);                                    \
        }                                                           \
    } while (0)
