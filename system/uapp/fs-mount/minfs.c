// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/processargs.h>
#include <mxio/util.h>

#include "mount.h"

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00,
    0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

bool minfs_detect(const uint8_t* data) {
    if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        return true;
    }
    return false;
}

int minfs_mount(mount_options_t* options) {
    mx_handle_t h;
    mx_status_t status = mount_remote_handle(options->mountpath, &h);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to mount remote handle handle on mount path\n");
        return -1;
    }

    xprintf("fs_mount: Launching Minfs [%s]\n", options->devicepath);
    const char* argv[] = { "/boot/bin/minfs", options->devicepath, "mount" };
    return launch(arraylen(argv), argv, h);
}
