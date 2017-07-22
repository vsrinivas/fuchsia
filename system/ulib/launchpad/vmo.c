// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

mx_status_t launchpad_vmo_from_file(const char* filename, mx_handle_t* out) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return MX_ERR_IO;
    mx_status_t status = mxio_get_vmo(fd, out);
    close(fd);

    if (status == MX_OK) {
        if (strlen(filename) >= MX_MAX_NAME_LEN) {
            const char* p = strrchr(filename, '/');
            if (p != NULL) {
                filename = p + 1;
            }
        }

        mx_object_set_property(*out, MX_PROP_NAME, filename, strlen(filename));
    }

    return status;
}
