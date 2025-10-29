include(CompilerOptions)

macro(wayland_gui_target_set_default_flags
    target
    is_debug
    save_temporary_files)

    # -std=c++20

    set(common_flags -pthread)
    set(debug_flags "")
    set(release_flags "")

    if(${save_temporary_files})
        set(common_flags "${common_flags}" "-g" "-save-temps")
    endif()

    generate_common_flags(
        common_flags "${common_flags}"
        debug_flags "${debug_flags}"
        release_flags "${release_flags}"
    )

    # This apperantly needs to be set after all other flags. Probably because of some ordering problem.
    set(common_flags ${common_flags}
        -Wno-gnu-zero-variadic-macro-arguments # Supress warning for " , ##__VA_ARGS__ " in variadic macros
    )

    if(${is_debug})
        target_compile_options(${target} PRIVATE ${common_flags} ${debug_flags})
    else()
        target_compile_options(${target} PRIVATE ${common_flags} ${release_flags})
    endif()

endmacro()

