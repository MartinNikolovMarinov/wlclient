# Events left:
0. Window visibility (on/off)
1. Window focus (gained/lost)
2. Window minimize
3. Window maximize
4. Window input focus - for text boxes
5. Window width and height limits

Maybe I should have a "Window demage and refresh" to notify the user when a window has been demaged. That might trigger IO components redraw, or something like that.

# Other features:
1. Set window icon
2. Set cursor
3. Set window opacity and investigate why alpha blending is broken ?
4. Monitor configurations and tests with multi-monotor setup.

# Valgrind errors

There are quite a lot of Valgrind errors during execution. Investigate what is going on. Write a suppression for the problems that come from drivers.
