// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resources.h"

#include <magenta/device/sysinfo.h>
#include <magenta/status.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

mx_status_t get_root_resource(mx_handle_t* root_resource) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open sysinfo: %s (%d)\n",
                strerror(errno), errno);
        return MX_ERR_NOT_FOUND;
    }

    ssize_t n = ioctl_sysinfo_get_root_resource(fd, root_resource);
    close(fd);
    if (n != sizeof(*root_resource)) {
        if (n < 0) {
            fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%zd)\n",
                    mx_status_get_string(n), n);
            return (mx_status_t)n;
        } else {
            fprintf(stderr, "ERROR: Cannot obtain root resource (%zd != %zd)\n",
                    n, sizeof(root_resource));
            return MX_ERR_NOT_FOUND;
        }
    }
    return MX_OK;
}
