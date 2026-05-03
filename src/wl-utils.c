#define _GNU_SOURCE

#include "arch.h"
#include "wl-utils.h"

#include <time.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h> // IWYU pragma: keep

#if defined(CPU_ARCH_X86_64) && CPU_ARCH_X86_64 == 1
#include <x86intrin.h>
#endif

u64 wlclient_get_unix_timestamp_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    u64 res = (u64)(ts.tv_sec) * 1000 + (u64)(ts.tv_nsec) / 1000000;
    return res;
}

u64 wlclient_get_monotonic_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    u64 res = (u64)(ts.tv_sec) * 1000000000ULL + (u64)(ts.tv_nsec);
    return res;
}

u64 wlclient_get_perf_counter(void) {
#if defined(CPU_ARCH_X86_64) && CPU_ARCH_X86_64 == 1
    return __rdtsc();
#elif defined(CPU_ARCH_ARM64) && CPU_ARCH_ARM64 == 1
    u64 counter;
    asm volatile("mrs %0, cntvct_el0" : "=r" (counter));
    return counter;
#else
    return wlclient_get_monotonic_now_ns();
#endif
}

u64 wlclient_get_cpu_frequency_hz(void) {
    static u64 frequency = 0;
    if (frequency > 0) return frequency;

#if defined(CPU_ARCH_ARM64) && (CPU_ARCH_ARM64 == 1)
    // On ARM64, the frequency of the virtual counter is available in CNTFRQ_EL0.
    asm volatile("mrs %0, cntfrq_el0" : "=r" (frequency));
#elif defined(CPU_ARCH_X86_64) && (CPU_ARCH_X86_64 == 1)
    // On x86_64, we calibrate the TSC over a fixed sleep interval.
    u64 start = wlclient_get_monotonic_now_ns();
    u64 tscStart = wlclient_get_perf_counter();
    struct timespec sleepTime = { 0, 100 * WLCLIENT_MILLISECOND };

    nanosleep(&sleepTime, NULL);

    u64 end = wlclient_get_monotonic_now_ns();
    u64 tscEnd = wlclient_get_perf_counter();
    u64 elapsedNs = end - start;
    u64 tscDiff   = tscEnd - tscStart;

    if (elapsedNs == 0) return 1;

    // Calculate frequency in Hz: (tscDiff ticks in elapsedNs nanoseconds).
    frequency = (tscDiff * WLCLIENT_SECOND) / elapsedNs;
#endif

    return frequency;
}

struct timespec wlclient_ns_to_timespec(u64 ns) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / WLCLIENT_SECOND);
    ts.tv_nsec = (__typeof__(ts.tv_nsec))(ns % WLCLIENT_SECOND);
    return ts;
}

WLCLIENT_API_INTERNAL struct wlclient_poll_result wlclient_poll_with_timeout(
    struct pollfd *fds,
    nfds_t nfds,
    u64 nanoseconds
) {
    const bool is_blocking = (nanoseconds == 0);
    i32 poll_res = 0;
    bool timedout = false;

    while (true) {
        if (is_blocking) {
            poll_res = poll(fds, nfds, -1);
            if (poll_res >= 0) {
                break;
            }
            else if (errno != EINTR && errno != EAGAIN) {
                break;
            }
        }
        else {
            u64 start_ns = wlclient_get_monotonic_now_ns();

            const struct timespec ts = wlclient_ns_to_timespec(nanoseconds);
            poll_res = ppoll(fds, nfds, &ts, NULL);

            if (poll_res > 0) {
                break;
            }
            else if (poll_res == 0) {
                timedout = true;
                break;
            }
            else if (errno != EINTR && errno != EAGAIN) {
                break;
            }

            u64 elapsed_ns = wlclient_get_monotonic_now_ns() - start_ns;
            if (elapsed_ns >= nanoseconds) {
                timedout = true;
                poll_res = 0;
                break;
            }

            nanoseconds -= elapsed_ns;
        }
    }

    struct wlclient_poll_result ret = { poll_res, timedout };
    return ret;
}
