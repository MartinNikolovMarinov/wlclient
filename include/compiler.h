#pragma once

#if defined(__clang__)
    #define COMPILER_CLANG 1
#else
    #define COMPILER_CLANG 0
#endif
#if (defined(__GNUC__) || defined(__GNUG__)) && COMPILER_CLANG == 0
    #define COMPILER_GCC 1
#else
    #define COMPILER_GCC 0
#endif
#if defined(_MSC_VER)
    #define COMPILER_MSVC 1
#else
    #define COMPILER_MSVC 0
#endif
#if COMPILER_CLANG == 0 && COMPILER_GCC == 0 && COMPILER_MSVC == 0
    #define COMPILER_UNKNOWN 1
#else
    #define COMPILER_UNKNOWN 0
#endif

#if defined(COMPILER_MSVC) && COMPILER_MSVC == 1
    #define PRAGMA_WARNING_PUSH __pragma(warning(push))
    #define PRAGMA_WARNING_POP __pragma(warning(pop))
    #define DISABLE_MSVC_WARNING(w) __pragma(warning(disable : w))
    #define PRAGMA_COMPILER_MESSAGE(x) __pragma(message(#x))
    #define PRAGMA_WARNING_SUPPRESS_ALL \
            __pragma(warning(push, 0))
#endif

#if defined(COMPILER_GCC) && COMPILER_GCC == 1
    #define PRAGMA_WARNING_PUSH _Pragma("GCC diagnostic push")
    #define PRAGMA_WARNING_POP _Pragma("GCC diagnostic pop")
    #define _QUOTED_PRAGMA(x) _Pragma (#x)
    #define DISABLE_GCC_AND_CLANG_WARNING(w) _QUOTED_PRAGMA(GCC diagnostic ignored #w)
    #define DISABLE_GCC_WARNING(w) _QUOTED_PRAGMA(GCC diagnostic ignored #w)
    #define PRAGMA_COMPILER_MESSAGE(x) _QUOTED_PRAGMA(message #x)

    #define PRAGMA_WARNING_SUPPRESS_ALL                                    \
        _Pragma("GCC diagnostic push")                                     \
        _Pragma("GCC diagnostic ignored \"-Wall\"")                        \
        _Pragma("GCC diagnostic ignored \"-Wextra\"")                      \
        _Pragma("GCC diagnostic ignored \"-Wpedantic\"")                   \
        _Pragma("GCC diagnostic ignored \"-Wconversion\"")                 \
        _Pragma("GCC diagnostic ignored \"-Wsign-conversion\"")            \
        _Pragma("GCC diagnostic ignored \"-Wcast-qual\"")                  \
        _Pragma("GCC diagnostic ignored \"-Wshadow\"")                     \
        _Pragma("GCC diagnostic ignored \"-Wdouble-promotion\"")           \
        _Pragma("GCC diagnostic ignored \"-Wfloat-equal\"")                \
        _Pragma("GCC diagnostic ignored \"-Wformat=2\"")                   \
        _Pragma("GCC diagnostic ignored \"-Wstrict-aliasing\"")            \
        _Pragma("GCC diagnostic ignored \"-Wundef\"")                      \
        _Pragma("GCC diagnostic ignored \"-Wnull-dereference\"")           \
        _Pragma("GCC diagnostic ignored \"-Wswitch-enum\"")                \
        _Pragma("GCC diagnostic ignored \"-Wincompatible-pointer-types\"")
#endif

#if defined(COMPILER_CLANG) && COMPILER_CLANG == 1
    #define PRAGMA_WARNING_PUSH _Pragma("clang diagnostic push")
    #define PRAGMA_WARNING_POP _Pragma("clang diagnostic pop")
    #define _QUOTED_PRAGMA(x) _Pragma (#x)
    #define DISABLE_GCC_AND_CLANG_WARNING(w) _QUOTED_PRAGMA(clang diagnostic ignored #w)
    #define DISABLE_CLANG_WARNING(w) _QUOTED_PRAGMA(clang diagnostic ignored #w)
    #define PRAGMA_COMPILER_MESSAGE(x) _QUOTED_PRAGMA(message #x)
    #define PRAGMA_WARNING_SUPPRESS_ALL \
        _Pragma("clang diagnostic push") \
        _Pragma("clang diagnostic ignored \"-Weverything\"")
#endif

#ifndef PRAGMA_WARNING_PUSH
    #define PRAGMA_WARNING_PUSH
#endif
#ifndef PRAGMA_WARNING_POP
    #define PRAGMA_WARNING_POP
#endif
#ifndef DISABLE_MSVC_WARNING
    #define DISABLE_MSVC_WARNING(...)
#endif
#ifndef DISABLE_GCC_AND_CLANG_WARNING
    #define DISABLE_GCC_AND_CLANG_WARNING(...)
#endif
#ifndef DISABLE_GCC_WARNING
    #define DISABLE_GCC_WARNING(...)
#endif
#ifndef DISABLE_GCC_CXX_ONLY_WARNING_OLD_STYLE_CAST
    #define DISABLE_GCC_CXX_ONLY_WARNING_OLD_STYLE_CAST
#endif
#ifndef DISABLE_CLANG_WARNING
    #define DISABLE_CLANG_WARNING(...)
#endif
#ifndef PRAGMA_COMPILER_MESSAGE
    #define PRAGMA_COMPILER_MESSAGE(...)
#endif
#ifndef PRAGMA_WARNING_SUPPRESS_ALL
    #define PRAGMA_WARNING_SUPPRESS_ALL
#endif

#if COMPILER_CLANG == 1 || COMPILER_GCC == 1
    #define PRINTF_LIKE(fmt_index, first_arg_index) __attribute__((format(printf, fmt_index, first_arg_index)))
#else
    #define PRINTF_LIKE(fmt_index, first_arg_index)
#endif
