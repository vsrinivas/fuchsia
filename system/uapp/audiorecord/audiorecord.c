// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio.h>
#include <mxio/io.h>
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

        ioctl_audio_start(fd);

        closedir(dir);
        return fd;

next:
        close(fd);
    }

    closedir(dir);
    return -1;
}

int main(int argc, char **argv) {
    int fd = open_source();
    if (fd < 0) {
        printf("couldn't find a usable audio source\n");
        return -1;
    }

    while (1) {
        uint16_t buffer[500];
        mxio_wait_fd(fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int length = read(fd, buffer, sizeof(buffer));
        printf("read %d\n", length);
        if (length < 0) break;
    }

    close(fd);
    return 0;
}
