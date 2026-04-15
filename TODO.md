# Multi input

From what I understand from the documentation the only way to get reliable multi user input is through multiple seats. Which is a feature with some questionable support and requires system level configuration through `udev`/`libinput`, or what ever makes sense. The compositor support is also not exactly obvious.

TODO: I should still probably support some rudimentary seat logistics at least expose the seat id and support removing.
