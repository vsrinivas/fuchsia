// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/midi/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/types.h>

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

        fuchsia_hardware_midi_Info device_info;
        fdio_t* fdio = fdio_unsafe_fd_to_io(fd);
        zx_status_t status = fuchsia_hardware_midi_DeviceGetInfo(
                fdio_unsafe_borrow_channel(fdio), &device_info);
        fdio_unsafe_release(fdio);
        if (status != ZX_OK) {
            printf("fuchsia.hardware.midi.Device/GetInfo failed for %s\n", devname);
            close(fd);
            continue;
        }
        if (device_info.is_source) {
            if (src_fd == -1) {
                src_fd = fd;
            } else {
                close(fd);
            }
        } else if (device_info.is_sink) {
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
