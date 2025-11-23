cmake_minimum_required(VERSION 3.5)
project(wayland-gui VERSION 0.0.1 LANGUAGES C CXX)

set(target_gui wayland-gui)
set(target_loader wayland-gui-loader)
set(target_sandbox sandbox)

# Standard Requirements:

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Generates compile_commands.json

# Add directories from lib cmake projects

set(CORE_ASSERT_ENABLED ON CACHE BOOL "Enable Asserts in corelib" FORCE)
add_subdirectory(lib/core)

# Include cmake modules from:

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_local")

include(CompilerOptions)
include(Logger)
include(WaylandGUIClangOptions)

init_logger("[WAYLAND_GUI]")

# ---------------------------------------- Begin Options ---------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Release")
    set(WAYLAND_APP_DEBUG OFF)
else()
    set(WAYLAND_APP_DEBUG ON)
endif()

option(USE_ANSI_LOGGING "Use ANSI escape codes in logging." OFF)
option(HOT_SWAP "Build with hotswapping functionality." OFF)

option(CORE_LIBRARY_SHARED "Build core as a shared library." OFF)
option(CORE_ASSERT_ENABLED "Enable asserts." OFF)

# Print Selected Options:

log_info("---------------------------------------------")
log_info("Version:           ${PROJECT_VERSION}")
log_info("Platform:          ${OS}")
log_info("Arch:              ${CMAKE_SYSTEM_PROCESSOR}")
log_info("C++ Version:       ${CMAKE_CXX_STANDARD}")
log_info("Compiler:          ${CMAKE_CXX_COMPILER_ID}")
log_info("Debug:             ${WAYLAND_APP_DEBUG}")
log_info("Use ANSI loagging: ${USE_ANSI_LOGGING}")
log_info("Hotswap:           ${HOT_SWAP}")
log_info("---------------------------------------------")

# ---------------------------------------- End Options -----------------------------------------------------------------

# ---------------------------------------- Begin Declare Source Files --------------------------------------------------

set(src_sandbox
    src/core_init.cpp

    lib/wayland/xdg-shell-client-protocol.c
)

set(src_gui
    src/core_init.cpp
    src/entry.cpp
    src/wayland_renderer.cpp
    src/wayland_platform.cpp

    lib/wayland/xdg-shell-client-protocol.c
)

# ---------------------------------------- End Declare Source Files ----------------------------------------------------

# ---------------------------------------- Begin Create Executable -----------------------------------------------------

macro(common_target_setup target)
    target_include_directories(${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/wayland
    )
    target_compile_definitions(${target} PUBLIC
        "USE_ANSI_LOGGING=$<BOOL:${USE_ANSI_LOGGING}>"
        "WAYLAND_APP_DEBUG=$<BOOL:${WAYLAND_APP_DEBUG}>"
    )
    wayland_gui_target_set_default_flags(${target} ${WAYLAND_APP_DEBUG} false)
endmacro()

if(!HOT_SWAP)
    # GUI shared library (reloadable)
    add_library(${target_gui} SHARED ${src_gui})
    target_link_libraries(${target_gui} PRIVATE core wayland-client)
    common_target_setup(${target_gui})

    # Force all symbols to be exported for debugging and hot-swap
    target_compile_options(${target_gui} PRIVATE
        -fvisibility=default
    )
    set_target_properties(${target_gui} PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )

    # Loader executable (for hot-swap in debug)
    add_executable(${target_loader} main-loader.cpp)
    target_link_libraries(${target_loader} PRIVATE dl)
    common_target_setup(${target_loader})
    target_compile_definitions(${target_loader} PUBLIC
        GUI_LIB_FILE_NAME=\"lib${target_gui}.so\"
        GUI_LIB_PATH=\"${PROJECT_BINARY_DIR}\"
    )
    target_include_directories(${target_loader} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/core/include
    )
else()
    # GUI shared library (reloadable)
    add_executable(${target_gui} main.cpp ${src_gui})
    target_link_libraries(${target_gui} PUBLIC core wayland-client)
    common_target_setup(${target_gui})
endif()

# Sandbox (untouched)
add_executable(${target_sandbox} sandbox.cpp ${src_sandbox})
target_link_libraries(${target_sandbox} PUBLIC core wayland-client)
common_target_setup(${target_sandbox})

# ---------------------------------------- END Create Executable -------------------------------------------------------
