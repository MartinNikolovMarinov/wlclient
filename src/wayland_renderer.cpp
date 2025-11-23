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

namespace {

SoftwareRenderingContext g_sharedRenderCtx;

FrameBuffer& getFrameBuffer();
void frameDone(void*, wl_callback* cb, u32);
void requestFrameCallback();
void validateRenderContext();

wl_callback* g_frameCallback = nullptr;
wl_callback_listener g_frameCallbackListener = {};
bool g_canRender = true;

} // namespace

void rendererInit() {
    Panic(core::loggerSetTag(i32(LoggerTags::T_RENDERER), "WAYLAND_RENDERER"_sv));
    core::loggerSetLevel(core::LogLevel::L_TRACE, LoggerTags::T_RENDERER);

    LOG_INFO_BLOCK_INIT_SECTION(LoggerTags::T_RENDERER, "Software Renderer");

    platformCreateSoftRendCtx(g_sharedRenderCtx);
    validateRenderContext();
}

bool renderFrame() {
    if (!g_canRender) {
        logTraceTagged(LoggerTags::T_RENDERER, "Skipping render; waiting for frame callback.");
        return false;
    }

    logTraceTagged(LoggerTags::T_RENDERER, "Rendering..");

    bool& bufferIsReady = *g_sharedRenderCtx.bufferIsReady;
    auto surface = g_sharedRenderCtx.surface;
    auto display = g_sharedRenderCtx.display;
    auto buffer = getFrameBuffer().wlBuffer;
    auto width = getFrameBuffer().width;
    auto height = getFrameBuffer().height;

    if (!bufferIsReady) {
        // This might be redundant.
        logWarnTagged(LoggerTags::T_RENDERER, "Buffer not ready.");
        return false;
    }
    bufferIsReady = false;

    // Binds a wl_buffer to a surface for the next frame. The buffer holds the pixel data you want displayed. The attach
    // itself does nothing visible until you commit:
    wl_surface_attach(surface, buffer, 0, 0);
    // Marks a region of the buffer as changed. The compositor will re-sample that area from your attached buffer when
    // the surface is committed. Without a damage call, the compositor may skip repainting:
    wl_surface_damage_buffer(surface, 0, 0, width, height);

    // Request a frame callback for this commit so we only render when the compositor is ready.
    requestFrameCallback();

    // Finalizes the pending state changes (attach, damage, etc.). Once you commit, the compositor treats that buffer as
    // the new frame and later releases it when it’s no longer in use.
    wl_surface_commit(surface);
    // Finalizes the pending state changes (attach, damage, etc.). Once you commit, the compositor treats that buffer as
    // the new frame and later releases it when it’s no longer in use.
    i32 _ = wl_display_flush(display); // ignoring the error here for performance reasons.

    return true;
}

void rendererShutdown() {
    LOG_INFO_BLOCK_SHUTDOWN(LoggerTags::T_RENDERER, "Software Renderer");
    if (g_frameCallback) {
        wl_callback_destroy(g_frameCallback);
        g_frameCallback = nullptr;
    }
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

void frameDone(void*, wl_callback* cb, u32) {
    if (cb) wl_callback_destroy(cb);
    g_frameCallback = nullptr;
    g_canRender = true;
}

void requestFrameCallback() {
    if (g_frameCallback) return; // already pending
    g_canRender = false;
    g_frameCallbackListener.done = frameDone;
    g_frameCallback = wl_surface_frame(g_sharedRenderCtx.surface);
    Panic(g_frameCallback, "wl_surface_frame returned null");
    i32 ret = wl_callback_add_listener(g_frameCallback, &g_frameCallbackListener, nullptr);
    PanicFmt(ret == 0, "wl_callback_add_listener exited with {}", ret);
}

void validateRenderContext() {
    Panic(g_sharedRenderCtx.bufferIsReady, "Render context missing buffer readiness flag");
    Panic(g_sharedRenderCtx.surface, "Render context missing surface");
    Panic(g_sharedRenderCtx.display, "Render context missing display");
    Panic(g_sharedRenderCtx.frameBuffers.len() > 0, "Render context missing framebuffer");

    FrameBuffer& fb = g_sharedRenderCtx.frameBuffers.first();
    Panic(fb.wlBuffer, "Framebuffer missing wl_buffer");
    Panic(fb.data, "Framebuffer missing mapped memory");
    Panic(fb.width > 0 && fb.height > 0, "Framebuffer has invalid dimensions");
    Panic(fb.pixelFormat == PixelFormat::ARGB8888, "Unsupported pixel format");
}

} // namespace
