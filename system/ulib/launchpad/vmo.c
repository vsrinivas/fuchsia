// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/vmo.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

zx_status_t launchpad_vmo_from_file(const char* filename, zx_handle_t* out) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return ZX_ERR_IO;
    zx_status_t status = fdio_get_vmo_clone(fd, out);
    close(fd);

    if (status == ZX_OK) {
        if (strlen(filename) >= ZX_MAX_NAME_LEN) {
            const char* p = strrchr(filename, '/');
            if (p != NULL) {
                filename = p + 1;
            }
        }

        zx_object_set_property(*out, ZX_PROP_NAME, filename, strlen(filename));
    }

    return status;
}
