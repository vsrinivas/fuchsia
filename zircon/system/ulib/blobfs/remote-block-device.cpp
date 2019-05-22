// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/block-device.h>
#include <blobfs/common.h>

namespace blobfs {

RemoteBlockDevice::RemoteBlockDevice(fbl::unique_fd device) : device_(std::move(device)) {}

zx_status_t RemoteBlockDevice::ReadBlock(uint64_t block_num, void* block) const {
    return readblk(device_.fd().get(), block_num, block);
}

zx_status_t RemoteBlockDevice::GetDevicePath(size_t buffer_len, char* out_name,
                                             size_t* out_len) const {
    if (buffer_len == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    zx_status_t status, io_status;
    io_status = fuchsia_device_ControllerGetTopologicalPath(device_.borrow_channel(),
                                                            &status, out_name, buffer_len - 1,
                                                            out_len);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    // Ensure null-terminated
    out_name[*out_len] = 0;
    // Account for the null byte in the length, since callers expect it.
    (*out_len)++;
    return ZX_OK;
}

zx_status_t RemoteBlockDevice::BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const {
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(device_.borrow_channel(),
                                                                &status, out_info);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::BlockGetFifo(zx::fifo* out_fifo) const {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockGetFifo(device_.borrow_channel(), &status,
                                                    out_fifo->reset_and_get_address());
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::BlockCloseFifo() {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockCloseFifo(device_.borrow_channel(), &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::BlockAttachVmo(zx::vmo vmo,
        fuchsia_hardware_block_VmoID* out_vmoid) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockAttachVmo(device_.borrow_channel(), vmo.release(),
                                                      &status, out_vmoid);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeQuery(
        fuchsia_hardware_block_volume_VolumeInfo* out_info) const {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeQuery(device_.borrow_channel(), &status,
                                                          out_info);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeQuerySlices(
        const uint64_t* slices, size_t slices_count,
        fuchsia_hardware_block_volume_VsliceRange* out_ranges, size_t* out_ranges_count) const {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
        device_.borrow_channel(), slices, slices_count, &status, out_ranges, out_ranges_count);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeExtend(uint64_t offset, uint64_t length) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeExtend(device_.borrow_channel(), offset,
                                                           length, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeShrink(uint64_t offset, uint64_t length) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeShrink(device_.borrow_channel(), offset,
                                                           length, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

} // namespace blobfs
