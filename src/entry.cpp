#include "entry.h"
#include "core_init.h"

#include "logger_config.h"
#include "platform.h"
#include "renderer.h"

extern "C" {

enum struct ExitCodes : i32 {
    GRACEFUL_SHUTDOWN = 0,
    RUNNING = 1,
    IS_READY_TO_RELOAD_RETURN_CODE = 6969
};

static constexpr f64 FRAME_RATE_CAP = 60.0;

[[maybe_unused]] static bool g_reloading = false;
static const u64 g_cpuFreq = core::getCPUFrequencyHz();

void entryInit(i32 argc, const char** argv) {
    (void)argc;
    (void)argv;

    coreInit();
    Panic(core::loggerSetTag(i32(LoggerTags::T_ENTRY), "ENTRY"_sv));

    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_ENTRY, "Application");

    OpenWindowInfo info = {};
    info.name = "Example Window";
    info.width = 600;
    info.height = 600;
    info.useSoftwareRendering = true;

    platformInit();
    platformOpenOSWindow(info);
    rendererInit();
}

namespace {

bool g_lastFrameRendered = false;

void setupScene() {
    if (!g_lastFrameRendered) return;

    static u16 tmpCounter = 1;

    u8 r = u8((tmpCounter/3)%255);
    u8 g = u8((tmpCounter/2)%255);
    u8 b = u8(tmpCounter%255);
    u8 a = u8(255);

    tmpCounter++;

    rendererClearScreen({.r = r, .g = g, .b = b, .a = a});

    i32 tmpWidth = 600;
    i32 tmpHeight = 600;
    renderDirectRect({.r = 255, .g = 0, .b = 0, .a = 255}, 0, 0, 50, 50);
    renderDirectRect({.r = 0, .g = 255, .b = 0, .a = 255}, tmpWidth - 50, tmpHeight - 50, 50, 50);
    renderDirectRect({.r = 0, .g = 0, .b = 255, .a = 255}, tmpWidth - 50, 0, 50, 50);
    renderDirectRect({.r = 0, .g = 255, .b = 255, .a = 255}, 0, tmpHeight - 50, 50, 50);
}

} // namespace

i32 entryMain() {
    u64 start = core::getPerfCounter();

    if (g_reloading) {
        logInfoTagged(LoggerTags::T_ENTRY, "Is ready to be reloaded.");
        g_reloading = false; // no need for this, but keeping for consistency.
        return i32(ExitCodes::IS_READY_TO_RELOAD_RETURN_CODE);
    }

    platformPollEvents();
    setupScene();
    g_lastFrameRendered = renderFrame();

    u64 end = core::getPerfCounter();
    f64 frameTimeMs = (f64(end - start) / f64(g_cpuFreq)) * 1000.0;
    f64 ahead = 1000.0/FRAME_RATE_CAP - frameTimeMs;

    if (ahead > 0) {
        logDebugTagged(LoggerTags::T_ENTRY, "fps: {:f.4}, frame time: {:f.4}ms", f64(1000/frameTimeMs), frameTimeMs);
        logDebugTagged(LoggerTags::T_ENTRY, "ahead: {:f.4}ms", ahead);
        core::threadingSleep(u64(ahead));
    }
    else {
        // Dipping below target:
        logWarnTagged(LoggerTags::T_ENTRY, "fps: {:f.4}, frame time: {:f.4}ms", f64(1000/frameTimeMs), frameTimeMs);
    }

    return i32(ExitCodes::RUNNING);
}

void entryRecvNotifyReload() {
    logWarnTagged(LoggerTags::T_ENTRY, "Detected rebuild — preparing reload...");
    g_reloading = true;
}

void entryShutdown() {
    {
        LOG_INFO_BLOCK_SHUTDOWN_SECTION(LoggerTags::T_ENTRY, "Application");

        rendererShutdown();
        platformShutdown();
    }

    coreShutdown(); // keep this as the last thing.
}

}
