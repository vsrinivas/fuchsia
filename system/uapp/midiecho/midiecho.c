// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/midi.h>
#include <mxio/io.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <magenta/types.h>

#define DEV_MIDI   "/dev/class/midi"

static bool open_devices(int* out_src_fd, int* out_dest_fd) {
    int src_fd = -1;
    int dest_fd = -1;

    struct dirent* de;
    DIR* dir = opendir(DEV_MIDI);
    if (!dir) {
        printf("Error opening %s\n", DEV_MIDI);
        return -1;
    }

    while ((de = readdir(dir)) != NULL && (src_fd == -1 || dest_fd == -1)) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_MIDI, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        int device_type;
        int ret = ioctl_midi_get_device_type(fd, &device_type);
        if (ret != sizeof(device_type)) {
            printf("ioctl_midi_get_device_type failed for %s\n", devname);
            close(fd);
            continue;
        }
        if (device_type == MIDI_TYPE_SOURCE) {
            if (src_fd == -1) {
                src_fd = fd;
            } else {
                close(fd);
            }
        } else if (device_type == MIDI_TYPE_SINK) {
            if (dest_fd == -1) {
                dest_fd = fd;
            } else {
                close(fd);
            }
        } else {
            close(fd);
        }
    }

    closedir(dir);
    if (src_fd == -1) {
        close(dest_fd);
        return false;
    }
    if (dest_fd == -1) {
        close(src_fd);
        return false;
    }

    *out_src_fd = src_fd;
    *out_dest_fd = dest_fd;
    return true;
}

int main(int argc, char **argv)
{
    int src_fd = -1, dest_fd = -1;
    if (!open_devices(&src_fd, &dest_fd)) {
        printf("couldn't find a usable MIDI source and sink\n");
        return -1;
    }

    while (1) {
        uint8_t buffer[3];

        mxio_wait_fd(src_fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int length = read(src_fd, buffer, sizeof(buffer));
        if (length < 0) break;
        printf("MIDI event:");
        for (int i = 0; i < length; i++) {
            printf(" %02X", buffer[i]);
        }
        printf("\n");
        if (write(dest_fd, buffer, length) < 0) break;
    }

    close(src_fd);
    close(dest_fd);

    return 0;
}
