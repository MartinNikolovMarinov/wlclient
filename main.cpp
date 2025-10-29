#include "core_init.h"
#include "entry.h"

using namespace coretypes;

constexpr i32 CONTINUE_TO_RUN_CODE = 1;

i32 main(i32 argc, const char** argv) {
    coreInit();

    entryInit(argc, argv);

    i32 ret = CONTINUE_TO_RUN_CODE;
    while (ret == CONTINUE_TO_RUN_CODE) {
        ret = entryMain();
    }

    entryShutdown();
    return 0;
}
