#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "renderer.h"
#include "platform.h"
#include "wayland_platform.h"
#include "logger_config.h"

// FIXME: A lot of error handling is still missing.

namespace {

SoftwareRenderingContext g_sharedRenderCtx;

FrameBuffer& getFrameBuffer();

} // namespace

void rendererInit() {
    Panic(core::loggerSetTag(i32(LoggerTags::T_RENDERER), "WAYLAND_RENDERER"_sv));
    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_RENDERER, "Software Renderer");

    platformCreateSoftRendCtx(g_sharedRenderCtx);

    for (addr_size i = 0; i < g_sharedRenderCtx.frameBuffers.len(); i++) {
        Panic(g_sharedRenderCtx.frameBuffers[i].pixelFormat == PixelFormat::ARGB8888,
            "The only currently supported pixel format is ARGB8888");
    }
}

bool renderFrame() {
    bool& bufferIsReady = *g_sharedRenderCtx.bufferIsReady;
    auto surface = g_sharedRenderCtx.surface;
    auto display = g_sharedRenderCtx.display;
    auto buffer = getFrameBuffer().wlBuffer;
    auto width = getFrameBuffer().width;
    auto height = getFrameBuffer().height;

    if (!bufferIsReady) {
        logWarnTagged(LoggerTags::T_RENDERER, "Buffer not ready.");
        return false;
    }
    bufferIsReady = false;

    logTraceTagged(LoggerTags::T_RENDERER, "Render Frame.");

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

    return true;
}

void rendererShutdown() {
    LOG_INFO_BLOCK_SHUTDOWN(LoggerTags::T_RENDERER, "Software Renderer");
    g_sharedRenderCtx = {};
}

void rendererClearScreen(Color color) {
    auto fb = getFrameBuffer();
    auto width = fb.width;
    auto height = fb.height;
    renderDirectRect(color, 0, 0, width, height);
}

void renderDirectRect(Color color, i32 x, i32 y, i32 width, i32 height) {
    auto fb = getFrameBuffer();
    u8 r = color.r;
    u8 g = color.g;
    u8 b = color.b;
    u8 a = color.a;
    auto frameBufferWidth = fb.width;
    auto pixelFormat = fb.pixelFormat;
    auto bytesPerPixel = getBytesPerPixel(pixelFormat);
    auto memoryMappedArea = fb.data;

    for (i32 row = y; row < y + height; ++row) {
        for (i32 col = x; col < x + width; ++col) {
            i32 offset = (row * frameBufferWidth + col) * bytesPerPixel;
            // WL_SHM_FORMAT_ARGB8888 is little-endian → BGRA byte order in memory.
            memoryMappedArea[offset + 0] = b;
            memoryMappedArea[offset + 1] = g;
            memoryMappedArea[offset + 2] = r;
            memoryMappedArea[offset + 3] = a;
        }
    }
}

namespace {

FrameBuffer& getFrameBuffer() {
    // TODO: implement double/tripple buffering
    return g_sharedRenderCtx.frameBuffers.first();
}

} // namespace
