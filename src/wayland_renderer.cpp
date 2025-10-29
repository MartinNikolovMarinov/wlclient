#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "renderer.h"
#include "platform.h"
#include "logger_config.h"

// FIXME: A lot of error handling is still missing.

namespace {

SoftwareRenderingContext g_sharedRenderCtx;

} // namespace

void rendererInit() {
    core::loggerSetTag(i32(LoggerTags::T_RENDERER), "WAYLAND_RENDERER"_sv);
    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_RENDERER, "Software Renderer");

    platformCreateSoftRendCtx(g_sharedRenderCtx);
}

void renderFrame() {
    bool& bufferIsReady = *g_sharedRenderCtx.bufferIsReady;
    auto surface = g_sharedRenderCtx.surface;
    auto buffer = g_sharedRenderCtx.buffer;
    auto display = g_sharedRenderCtx.display;
    auto width = g_sharedRenderCtx.frameBufferWidth;
    auto height = g_sharedRenderCtx.frameBufferHeight;
    auto memoryMappedArea = g_sharedRenderCtx.memoryMappedArea;

    constexpr i32 BITS_PER_PIXEL = 4; // TODO2: move somewhere.
    i32 size = width * height * BITS_PER_PIXEL;

    if (!bufferIsReady) {
        logInfoTagged(LoggerTags::T_RENDERER, "Buffer not ready.");
        return;
    }
    bufferIsReady = false;

    logTraceTagged(LoggerTags::T_RENDERER, "Render Frame.");

    // Render Frame
    static u16 tmpCounter = 1;
    for (i32 i = 0; i < size; i+=4) {
        memoryMappedArea[i] = u8(tmpCounter%255); // blue
        memoryMappedArea[i+1] = u8((tmpCounter/2)%255); // green
        memoryMappedArea[i+2] = u8((tmpCounter/3)%255); // red
        memoryMappedArea[i+3] = u8(255); // alpha
    }
    tmpCounter++;

    // Binds a wl_buffer to a surface for the next frame. The buffer holds the pixel data you want displayed. The attach
    // itself does nothing visible until you commit:
    wl_surface_attach(surface, buffer, 0, 0);
    // Marks a region of the buffer as changed. The compositor will re-sample that area from your attached buffer when
    // the surface is committed. Without a damage call, the compositor may skip repainting:
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    // Finalizes the pending state changes (attach, damage, etc.). Once you commit, the compositor treats that buffer as
    // the new frame and later releases it when it’s no longer in use.
    wl_surface_commit(surface);
    // Finalizes the pending state changes (attach, damage, etc.). Once you commit, the compositor treats that buffer as
    // the new frame and later releases it when it’s no longer in use.
    wl_display_flush(display);
}

void rendererShutdown() {
    LOG_INFO_BLOCK_SHUTDOWN(LoggerTags::T_RENDERER, "Software Renderer");
    g_sharedRenderCtx = {};
}
