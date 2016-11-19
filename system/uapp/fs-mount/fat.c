// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>

#include "mount.h"

bool fat_detect(const uint8_t* data) {
    if ((data[510] == 0x55 && data[511] == 0xAA) &&
        (data[38] == 0x29 || data[66] == 0x29)) {
        // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
        // 0x29 is the Boot Signature, but it is placed at either offset 38 or
        // 66 (depending on FAT type).
        return true;
    }
    return false;
}

int fat_mount(mount_options_t* options) {
    mx_handle_t h;
    mx_status_t status = mount_remote_handle(options->mountpath, &h);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to mount remote handle handle on mount path\n");
        return -1;
    }

    char device_path_arg[MXIO_MAX_FILENAME + 64];
    snprintf(device_path_arg, sizeof(device_path_arg), "-devicepath=%s",
             options->devicepath);

    char readonly_arg[64];
    snprintf(readonly_arg, sizeof(readonly_arg), "-readonly=%s",
             options->readonly ? "true" : "false");

    xprintf("fs_mount: Launching ThinFS [%s]\n", options->devicepath);
    const char* argv[] = {
        "/system/bin/thinfs",
        device_path_arg,
        readonly_arg,
        "mount",
    };
    return launch(arraylen(argv), argv, h);
}
