// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/vmo.h>
#include <mxio/io.h>

#include <fcntl.h>
#include <unistd.h>

// TODO(mcgrathr): Drop this interface altogether, since mxio_get_vmo
// is the same thing.  At that point, perhaps drop launchpad_vmo_from_file
// too, since it's so thin.
mx_handle_t launchpad_vmo_from_fd(int fd) {
    mx_handle_t vmo;
    mx_status_t status = mxio_get_vmo(fd, &vmo);
    return status == NO_ERROR ? vmo : status;
}

mx_handle_t launchpad_vmo_from_file(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return ERR_IO;
    mx_handle_t vmo = launchpad_vmo_from_fd(fd);
    close(fd);
    return vmo;
}
