#include "core_init.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

i32 main() {
    const char *device = "/dev/input/event3";
    int fd = open(device, O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct input_event ev;

    while (1) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            printf("time %ld.%06ld  type %u  code %u  value %d\n",
                   ev.time.tv_sec,
                   ev.time.tv_usec,
                   ev.type,
                   ev.code,
                   ev.value);
        }
    }

    close(fd);
    return 0;
}
