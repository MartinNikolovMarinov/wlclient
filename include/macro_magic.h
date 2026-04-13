#pragma once

#define WLCLIENT_STRINGIFY_IMPL(x) #x
#define WLCLIENT_STRINGIFY(x) WLCLIENT_STRINGIFY_IMPL(x)
#define WLCLIENT_CONCAT_IMPL(a, b) a##b
#define WLCLIENT_CONCAT(a, b) WLCLIENT_CONCAT_IMPL(a, b)

#define WLCLIENT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WLCLIENT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define WLCLIENT_CLAMP(x, min, max) WLCLIENT_MIN(WLCLIENT_MAX(x, min), max)

#define WLCLIENT_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
