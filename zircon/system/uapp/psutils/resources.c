// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resources.h"

#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/util.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

zx_status_t get_root_resource(zx_handle_t* root_resource) {
    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open sysinfo: %s (%d)\n",
                strerror(errno), errno);
        return ZX_ERR_NOT_FOUND;
    }

    zx_handle_t channel;
    zx_status_t status = fdio_get_service_handle(fd, &channel);
    if (status != ZX_OK) {
        fprintf(stderr, "ERROR: Cannot obtain sysinfo channel: %s (%d)\n",
                zx_status_get_string(status), status);
        return status;
    }

    zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(channel, &status, root_resource);
    zx_handle_close(channel);

    if (fidl_status != ZX_OK) {
        fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%d)\n",
                zx_status_get_string(fidl_status), fidl_status);
        return fidl_status;
    } else if (status != ZX_OK) {
        fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%d)\n",
                zx_status_get_string(status), status);
        return status;
    }

    return ZX_OK;
}
