# Window events left

1. Allow window to start in fullscreen.

Maybe I should have a "Window demage and refresh" to notify the user when a window has been demaged. That might trigger IO components redraw, or something like that.

# Other features

1. Set window icon.
2. Change cursor icon. Cursor animations will be a pain.
3. Set window opacity and investigate why alpha blending is broken ?
4. Monitor configurations and tests with multi-monotor setup.
5. Multi-window testing has not been done at all.
6. Fractional scaling.
7. Library distribution strategy. Some `typedef`s are also leaking into the global scope.
8. Performance measuring?

# Mouse Input events left

1. Axis events.
2. Decoration minimize, maximize and hide

"Set cursor position" might be a function I want.

# Valgrind errors

There are quite a lot of Valgrind errors during execution. Investigate what is going on. Write a suppression for the problems that come from drivers.
