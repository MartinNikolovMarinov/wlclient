#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <assert.h>
#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>

#include <sys/inotify.h>

#include "core_types.h"

#define GUI_LIB_FILE_PATH (GUI_LIB_PATH "/" GUI_LIB_FILE_NAME)

using namespace coretypes;

using EntryInit_t = void (*)(i32, const char**);
using EntryMain_t = i32 (*)();
using EntryRecvNotifyReload_t = void (*)();
using EntryShutdown_t = void (*)();

constexpr i32 IS_READY_TO_RELOAD_RETURN_CODE = 6969;
constexpr i32 CONTINUE_TO_RUN_CODE = 1;

EntryInit_t g_entryInit = nullptr;
EntryMain_t g_entryMain = nullptr;
EntryShutdown_t g_entryShutdown = nullptr;
EntryRecvNotifyReload_t g_entryRecvNotifyReload  = nullptr;

std::atomic<bool> g_reloadRequested{false};
bool g_reloadInProgress = false;

bool loadSymbols(void*& h) {
    h = dlopen(GUI_LIB_FILE_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return false;
    }

    g_entryInit = reinterpret_cast<EntryInit_t>(dlsym(h, "entryInit"));
    g_entryMain = reinterpret_cast<EntryMain_t>(dlsym(h, "entryMain"));
    g_entryShutdown = reinterpret_cast<EntryShutdown_t>(dlsym(h, "entryShutdown"));
    g_entryRecvNotifyReload = reinterpret_cast<EntryRecvNotifyReload_t>(dlsym(h, "entryRecvNotifyReload"));

    return g_entryInit && g_entryMain && g_entryShutdown && g_entryRecvNotifyReload;
}

void closeLib(void* h) {
    if (h) dlclose(h);

    g_entryInit = nullptr;
    g_entryMain = nullptr;
    g_entryShutdown = nullptr;
    g_entryRecvNotifyReload = nullptr;
}

void watchBuildDirectory(const char* watchDir, const char* watchFile) {
    int inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyFd < 0) {
        perror("inotify_init1");
        return;
    }

    int wd = inotify_add_watch(inotifyFd, watchDir, IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(inotifyFd);
        return;
    }

    struct pollfd pfd;
    pfd.fd = inotifyFd;
    pfd.events = POLLIN;

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    for (;;) {
        // poll blocking:
        int res = poll(&pfd, 1, -1);
        if (res <= 0) continue;

        // printf("[watcher] Woke Up\n");

        // reload was already requested:
        if (g_reloadRequested) continue;

        ssize_t len = read(inotifyFd, buf, sizeof(buf));
        if (len <= 0) continue;

        for (char* ptr = buf; ptr < buf + len; ) {
            auto* event = reinterpret_cast<struct inotify_event*>(ptr);
            if (event->len &&
                event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO) && // Sanity check
                strstr(event->name, watchFile)
            ) {
                printf("[watcher] requesting reload\n");
                g_reloadRequested.store(true, std::memory_order_relaxed);
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    inotify_rm_watch(inotifyFd, wd);
    close(inotifyFd);
}

i32 main(i32 argc, const char** argv) {

    void* libHandle = nullptr;
    if (!loadSymbols(libHandle))
        return 1;

    g_entryInit(argc, argv);

    std::thread(watchBuildDirectory, GUI_LIB_PATH, GUI_LIB_FILE_NAME).detach();

    i32 ret = 0;
    while (true) {
        ret = g_entryMain();

        if (ret == IS_READY_TO_RELOAD_RETURN_CODE) {
            // do reload
            printf("[main] reloading dynamic library\n");
            g_entryShutdown();
            closeLib(libHandle);
            if (!loadSymbols(libHandle))
                return 1;
            g_entryInit(argc, argv);
            g_reloadInProgress = false;
            g_reloadRequested.store(false, std::memory_order_relaxed);
        }
        else if (ret != CONTINUE_TO_RUN_CODE) {
            break;
        }

        if (!g_reloadInProgress && g_reloadRequested) {
            g_reloadInProgress = true;
            g_entryRecvNotifyReload();
        }
    }

    assert(ret == 0);

    g_entryShutdown();
    return ret;
}
