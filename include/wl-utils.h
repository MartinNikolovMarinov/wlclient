#pragma once

#include "API.h"
#include "types.h"

#include <poll.h>

struct wlclient_poll_result {
    i32 poll_result;
    bool timedout;
};

WLCLIENT_API_INTERNAL u64 wlclient_get_unix_timestamp_now_ms(void);
WLCLIENT_API_INTERNAL u64 wlclient_get_monotonic_now_ns(void);
WLCLIENT_API_INTERNAL u64 wlclient_get_perf_counter(void);

WLCLIENT_API_INTERNAL struct timespec wlclient_ns_to_timespec(u64 ns);

WLCLIENT_API_INTERNAL struct wlclient_poll_result wlclient_poll_with_timeout(
    struct pollfd *fds,
    nfds_t nfds,
    u64 nanoseconds
);
