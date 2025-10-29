#pragma once

#include "core_init.h"

enum LoggerTags : u8 {
    T_ENTRY = 1,
    T_RENDERER = 2,
    T_PLATFORM = 3,
};

#define LOG_INFO_BLOCK_INIT_SECTION(tag, msg) \
    defer { logSectionTitleInfoTagged((tag), msg" initialized. ✅"); };  \
    logSectionTitleInfoTagged((tag), msg" initializing...")

#define LOG_INFO_BLOCK_SHUTDOWN(tag, msg) \
    defer { logInfoTagged((tag), msg" shutdown. ✅"); };  \
    logInfoTagged((tag), msg" shutting down...")

#define LOG_INFO_BLOCK_SHUTDOWN_SECTION(tag, msg) \
    defer { logSectionTitleInfoTagged((tag), msg" shutdown. ✅"); };  \
    logSectionTitleInfoTagged((tag), msg" shutting down...")
