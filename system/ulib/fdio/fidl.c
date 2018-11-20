// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-fidl.h"

zx_status_t fidl_ioctl(zx_handle_t h, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual) {
    size_t in_handle_count = 0;
    size_t out_handle_count = 0;
    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        out_handle_count = 1;
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        out_handle_count = 2;
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        out_handle_count = 3;
        break;
    case IOCTL_KIND_SET_HANDLE:
        in_handle_count = 1;
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        in_handle_count = 2;
        break;
    }

    if (in_len < in_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (out_len < out_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t hbuf[out_handle_count];
    size_t out_handle_actual;
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeIoctl(h, op,
                                          out_len, (zx_handle_t*) in_buf,
                                          in_handle_count, in_buf,
                                          in_len, &status, hbuf,
                                          out_handle_count, &out_handle_actual,
                                          out_buf, out_len, out_actual)) != ZX_OK) {
        return io_status;
    }

    if (status != ZX_OK) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return status;
    }
    if (out_handle_actual != out_handle_count) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return ZX_ERR_IO;
    }

    memcpy(out_buf, hbuf, out_handle_count * sizeof(zx_handle_t));
    return ZX_OK;
}
