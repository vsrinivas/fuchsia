// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/debug.h>

#include <dirent.h>
#include <fcntl.h>

#include "xdc-init.h"

static const char* const DEV_XDC_DIR = "/dev/class/usb-dbc";

zx_status_t configure_xdc(const uint32_t stream_id, fbl::unique_fd& out_fd) {
    DIR* d = opendir(DEV_XDC_DIR);
    if (d == nullptr) {
        fprintf(stderr, "Could not open dir: \"%s\"\n", DEV_XDC_DIR);
        return ZX_ERR_BAD_STATE;
    }

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        int fd = openat(dirfd(d), de->d_name, O_RDWR);
        if (fd < 0) {
            continue;
        }
        zx_status_t status = static_cast<zx_status_t>(ioctl_debug_set_stream_id(fd, &stream_id));
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to set stream id %u for device \"%s/%s\", err: %d\n",
                    stream_id, DEV_XDC_DIR, de->d_name, status);
            close(fd);
            continue;
        }
        printf("Configured debug device \"%s/%s\", stream id %u\n",
               DEV_XDC_DIR, de->d_name, stream_id);
        out_fd.reset(fd);
        closedir(d);
        return ZX_OK;
    }
    closedir(d);

    fprintf(stderr, "No debug device found\n");
    return ZX_ERR_NOT_FOUND;
}

