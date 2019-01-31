// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#ifdef __Fuchsia__
#include <zircon/device/block.h>
#endif

namespace fs {

#ifdef __Fuchsia__

zx_status_t fvm_reset_volume_slices(int fd) {
    query_request_t request;
    query_response_t response;

    request.count = 1;
    request.vslice_start[0] = 1;

    while (true) {
        ssize_t r = ioctl_block_fvm_vslice_query(fd, &request, &response);
        zx_status_t status = (r <= 0) ? static_cast<zx_status_t>(r) : ZX_OK;
        if (status == ZX_ERR_OUT_OF_RANGE) {
            return ZX_OK;
        }
        if (status != ZX_OK) {
            return status;
        }
        if (response.count != 1 || response.vslice_range[0].count == 0) {
            return ZX_ERR_IO;
        }

        // Free any slices that were allocated
        if (response.vslice_range[0].allocated) {
            extend_request_t shrink;
            shrink.offset = request.vslice_start[0];
            shrink.length = response.vslice_range[0].count;

            r = ioctl_block_fvm_shrink(fd, &shrink);
            status = (r <= 0) ? static_cast<zx_status_t>(r) : ZX_OK;
            if (status != ZX_OK) {
                return status;
            }
        }

        // Move to the next portion of the block address space
        request.vslice_start[0] += response.vslice_range[0].count;
    }
}

#endif

} // namespace fs
