cmake_minimum_required(VERSION 3.21)
project(wlclient VERSION 0.1.0 LANGUAGES C)

# TODO: Add option to include everything into a single header STB type library.

set(target_wlclient wlclient)
set(target_sandbox sandbox)
set(target_test tests)

# Standard Requirements

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Generates compile_commands.json

# Include cmake modules

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Logger)

init_logger("[WAYLAND_CLIENT]")

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    include(ClangCompilerOptions)
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    include(GccCompilerOptions)
else()
    log_fatal("Unsupported compiler: ${CMAKE_C_COMPILER_ID}")
endif()

# ---------------------------------------- Begin Options ---------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Release")
    set(WLCLIENT_DEBUG OFF)
else()
    set(WLCLIENT_DEBUG ON)
endif()

if(NOT WLCLIENT_BACKEND OR WLCLIENT_BACKEND STREQUAL "NONE")
    set(WLCLIENT_BACKEND "NONE")
elseif(WLCLIENT_BACKEND STREQUAL "EGL")
    set(WLCLIENT_BACKEND_EGL TRUE)
elseif(WLCLIENT_BACKEND STREQUAL "VULKAN")
    set(WLCLIENT_BACKEND_VULKAN TRUE)
else()
    log_fatal("Unsupported backend: ${WLCLIENT_BACKEND}")
endif()

option(WLCLIENT_USE_LOGGING "Use ANSI escape codes in logging." OFF)
option(WLCLIENT_LIBRARY_SHARED "Build as shared library." OFF)
option(WLCLIENT_ASSERT_ENABLED "Enable asserts." OFF)
option(WLCLIENT_BUILD_SANDBOX "Build sandbox executable." OFF)
option(WLCLIENT_BUILD_TESTS "Build test executable." OFF)

# Print Selected Options:

log_info("---------------------------------------------")
log_info("Version:           ${PROJECT_VERSION}")
log_info("Platform:          ${CMAKE_SYSTEM_NAME}")
log_info("Arch:              ${CMAKE_SYSTEM_PROCESSOR}")
log_info("C Version:         ${CMAKE_C_STANDARD}")
log_info("Compiler:          ${CMAKE_C_COMPILER_ID}")
log_info("Debug:             ${WLCLIENT_DEBUG}")
log_info("Asserts:           ${WLCLIENT_ASSERT_ENABLED}")
log_info("Shared:            ${WLCLIENT_LIBRARY_SHARED}")
log_info("Backend:           ${WLCLIENT_BACKEND}")
log_info("Use logging:       ${WLCLIENT_USE_LOGGING}")
log_info("Build Sandbox:     ${WLCLIENT_BUILD_SANDBOX}")
log_info("Build Tests:       ${WLCLIENT_BUILD_TESTS}")
log_info("---------------------------------------------")

# ---------------------------------------- End Options -----------------------------------------------------------------

# ---------------------------------------- Begin Declare Source Files --------------------------------------------------

set(src_wlclient

    src/debug.c
    src/wl-client.c

    lib/wayland/xdg-shell-client-protocol.c
)

if (WLCLIENT_BACKEND_EGL)
    # set(src_wlclient ${src_wlclient} src/backends/wl-egl.c)
endif()

set(src_sandbox
    sandbox.c
)

set(src_test
    tests/unity/unity.c
    tests/test_main.c

    tests/t-basic-init.c
)

# ---------------------------------------- End Declare Source Files ----------------------------------------------------

# ---------------------------------------- Begin Create Executables ----------------------------------------------------

if(WLCLIENT_LIBRARY_SHARED)
    add_library(${target_wlclient} SHARED ${src_wlclient})
    # Set symbols visibility to be hidden by default. This is done to unify behavior across platforms.
    set_target_properties(${target_wlclient} PROPERTIES
        C_VISIBILITY_PRESET hidden
        C_VISIBILITY_INLINE_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_INLINE_PRESET hidden
    )
else()
    add_library(${target_wlclient} STATIC ${src_wlclient})
endif()

wlclient_target_set_default_c_flags(${target_wlclient} ${WLCLIENT_DEBUG} false)

target_include_directories(${target_wlclient} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/wayland
)
target_compile_definitions(${target_wlclient} PUBLIC
    "WLCLIENT_USE_LOGGING=$<BOOL:${WLCLIENT_USE_LOGGING}>"
    "WLCLIENT_DEBUG=$<BOOL:${WLCLIENT_DEBUG}>"
    "WLCLIENT_ASSERT_ENABLED=$<BOOL:${WLCLIENT_ASSERT_ENABLED}>"
)
target_compile_definitions(${target_wlclient} PRIVATE
    "WLCLIENT_LIBRARY_SHARED=$<BOOL:${WLCLIENT_LIBRARY_SHARED}>"
    "WLCLIENT_LIBRARY_BUILD=1"
)
target_link_libraries(${target_wlclient} PRIVATE wayland-client)

if (WLCLIENT_BACKEND_EGL)
    target_compile_definitions(${target_wlclient} PUBLIC "WLCLIENT_BACKEND_EGL=1")
elseif (WLCLIENT_BACKEND_VULKAN)
    target_compile_definitions(${target_wlclient} PUBLIC "WLCLIENT_BACKEND_VULKAN=1")
endif()

if (WLCLIENT_BUILD_SANDBOX)
    # Build the development code
    add_executable(${target_sandbox} ${src_sandbox})
    wlclient_target_set_default_c_flags(${target_sandbox} ${WLCLIENT_DEBUG} false)
    target_link_libraries(${target_sandbox} PUBLIC ${target_wlclient})
    target_link_libraries(${target_sandbox} PRIVATE wayland-egl EGL GLESv2)
endif()

if (WLCLIENT_BUILD_TESTS)
    add_executable(${target_test} ${src_test})
    wlclient_target_set_default_c_flags(${target_test} ${WLCLIENT_DEBUG} false)
    target_link_libraries(${target_test} PUBLIC ${target_wlclient})
    target_link_libraries(${target_test} PRIVATE wayland-egl EGL GLESv2)

    target_include_directories(${target_test} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unity
    )
endif()

# ---------------------------------------- END Create Executables ------------------------------------------------------
