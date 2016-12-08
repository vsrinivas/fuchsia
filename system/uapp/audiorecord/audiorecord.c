// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#define DEV_AUDIO   "/dev/class/audio"

static int open_source(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_AUDIO);
    if (!dir) {
        printf("Error opening %s\n", DEV_AUDIO);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_AUDIO, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        int device_type;
        int ret = ioctl_audio_get_device_type(fd, &device_type);
        if (ret != sizeof(device_type)) {
            printf("ioctl_audio_get_device_type failed for %s\n", devname);
            goto next;
        }
        if (device_type != AUDIO_TYPE_SOURCE) {
            goto next;
        }

        uint32_t sample_rate;
        ret = ioctl_audio_get_sample_rate(fd, &sample_rate);
        if (ret != sizeof(sample_rate)) {
            printf("%s unable to get sample rate\n", devname);
            goto next;
        }
        printf("%s sample rate %d\n", devname, sample_rate);

        closedir(dir);
        return fd;

next:
        close(fd);
    }

    closedir(dir);
    return -1;
}

static void usage(char* me) {
    fprintf(stderr, "usage: %s [-s <number of times to start/stop>] "
                    "[-r <number of buffers to read per start/stop>]\n", me);
}

int main(int argc, char **argv) {
    // number of times to start & stop audio
    int start_stop_count = 1;
    // number of times to read per start/stop
    int read_count = INT_MAX;

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (strcmp(arg, "-s") == 0) {
            if (++i < argc) {
                int count = atoi(argv[i]);
                if (count > 0) {
                    start_stop_count = count;
                    continue;
                }
            }
            usage(argv[0]);
            return -1;
        } else if (strcmp(arg, "-r") == 0) {
            if (++i < argc) {
                int count = atoi(argv[i]);
                if (count > 0) {
                    read_count = count;
                    continue;
                }
            }
            usage(argv[0]);
            return -1;
        } else {
            usage(argv[0]);
            return -1;
        }
    }


    int fd = open_source();
    if (fd < 0) {
        printf("couldn't find a usable audio source\n");
        return -1;
    }

    for (int i = 0; i < start_stop_count; i++) {
        ioctl_audio_start(fd);

        for (int j = 0; j < read_count; j++) {
            uint16_t buffer[500];
            int length = read(fd, buffer, sizeof(buffer));
            printf("read %d\n", length);
            if (length < 0) break;
        }

        ioctl_audio_stop(fd);
    }

    close(fd);
    return 0;
}
