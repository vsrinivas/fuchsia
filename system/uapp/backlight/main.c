// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/backlight.h>

static void usage(char* argv[]) {
    printf("Usage: %s [--read|--off|<brightness-val>]\n", argv[0]);
    printf("options:\n    <brightness-val>: 0-255\n");
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        usage(argv);
        return -1;
    }

    if (strcmp(argv[1], "--read") == 0) {
        int fd = open("/dev/class/backlight/000", O_RDONLY);
        if (fd < 0) {
            printf("Failed to open backlight\n");
            return -1;
        }

        backlight_state_t state;
        ssize_t ret = ioctl_backlight_get_state(fd, &state);
        if (ret < 0) {
            printf("Get backlight state ioctl failed\n");
            return -1;
        }
        printf("Backlight:%s Brightness:%d\n", state.on ? "on" : "off",
                state.brightness);
        return 0;
    }

    bool on;
    uint32_t brightness;
    if (strcmp(argv[1], "--off") == 0) {
        on = false;
        brightness = 0;
    } else {
        char* endptr;
        brightness = strtoul(argv[1], &endptr, 10);
        if (endptr == argv[1] || *endptr != '\0') {
            usage(argv);
            return -1;
        }
        if (brightness > 255) {
            printf("Invalid brightness %d\n", brightness);
            return -1;
        }
        on = true;
    }

    int fd = open("/dev/class/backlight/000", O_RDWR);
    if (fd < 0) {
        printf("Failed to open backlight\n");
        return -1;
    }

    backlight_state_t state = { .on = on, .brightness = (uint8_t) brightness };
    ssize_t ret = ioctl_backlight_set_state(fd, &state);
    if (ret < 0) {
        printf("Set brightness ioctl failed\n");
        return -1;
    }

    return 0;
}
