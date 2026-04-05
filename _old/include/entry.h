#pragma once

#include "core_types.h"

using namespace coretypes;

extern "C" {

// Called once on library load.
void entryInit(i32 argc, const char** argv);

// Called repeatedly by the loader main loop.
i32 entryMain();

// Called by the loader when it detects that a rebuild occurred,
// so the app can gracefully prepare for reload.
void entryRecvNotifyReload();

// Called to allow the app to run shutdown logic.
void entryShutdown();

}
