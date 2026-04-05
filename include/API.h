#pragma once

#if defined(WLCLIENT_LIBRARY_SHARED) && defined(WLCLIENT_LIBRARY_BUILD)
    #if defined(__GNUC__) || defined(__GNUG__) || defined(__clang__)
        #define WLCLIENT_API_EXPORT __attribute__((visibility("default")))
    #else
        #define WLCLIENT_API_EXPORT
    #endif
#else
    #define WLCLIENT_API_EXPORT
#endif
