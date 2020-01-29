// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fvm/client.h>

#include <stdlib.h>

#include <fbl/algorithm.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/device/block.h>

namespace fvm {

zx_status_t ResetAllSlices(const zx::unowned_channel& channel) {
  uint64_t vslice_start[1];
  vslice_start[0] = 1;

  while (true) {
    fuchsia_hardware_block_volume_VsliceRange
        ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
    size_t actual_ranges_count;
    zx_status_t io_status, status;
    io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
        channel->get(), vslice_start, fbl::count_of(vslice_start), &status, ranges,
        &actual_ranges_count);
    if (io_status != ZX_OK) {
      return io_status;
    }
    if (status == ZX_ERR_OUT_OF_RANGE) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      return status;
    }
    if (actual_ranges_count != 1 || ranges[0].count == 0) {
      return ZX_ERR_IO;
    }

    // Free any slices that were allocated.
    if (ranges[0].allocated) {
      io_status = fuchsia_hardware_block_volume_VolumeShrink(channel->get(), vslice_start[0],
                                                             ranges[0].count, &status);
      if (io_status != ZX_OK) {
        return io_status;
      }
      if (status != ZX_OK) {
        return status;
      }
    }

    // Move to the next portion of the block address space.
    vslice_start[0] += ranges[0].count;
  }
}

zx_status_t ResetAllSlices2(block_client::BlockDevice* device) {
  uint64_t vslice_start[1];
  vslice_start[0] = 1;

  while (true) {
    fuchsia_hardware_block_volume_VsliceRange
        ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
    size_t actual_ranges_count;
    zx_status_t status = device->VolumeQuerySlices(vslice_start, fbl::count_of(vslice_start),
                                                   ranges, &actual_ranges_count);
    if (status == ZX_ERR_OUT_OF_RANGE) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      return status;
    }
    if (actual_ranges_count != 1 || ranges[0].count == 0) {
      return ZX_ERR_IO;
    }

    // Free any slices that were allocated.
    if (ranges[0].allocated) {
      status = device->VolumeShrink(vslice_start[0], ranges[0].count);
      if (status != ZX_OK) {
        return status;
      }
    }

    // Move to the next portion of the block address space.
    vslice_start[0] += ranges[0].count;
  }
}

}  // namespace fvm
