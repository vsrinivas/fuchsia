// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/block-device.h>

#include <blobfs/common.h>
#include <fs/trace.h>
#include <fuchsia/io/c/fidl.h>

namespace blobfs {
namespace {

zx_status_t BlockGetFifo(const zx::channel& device, zx::fifo* out_fifo) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockGetFifo(device.get(), &status,
                                                    out_fifo->reset_and_get_address());
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t BlockCloseFifo(const zx::channel& device) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockCloseFifo(device.get(), &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

} // namespace

zx_status_t RemoteBlockDevice::ReadBlock(uint64_t block_num, uint64_t block_size,
                                         void* block) const {
    uint64_t offset = block_num * block_size;
    size_t actual;
    zx_status_t status, io_status;
    io_status = fuchsia_io_FileReadAt(device_.get(), block_size, offset, &status,
                                      reinterpret_cast<uint8_t*>(block), block_size, &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual != block_size) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t RemoteBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
    return fifo_client_.Transaction(requests, count);
}

zx_status_t RemoteBlockDevice::GetDevicePath(size_t buffer_len, char* out_name,
                                             size_t* out_len) const {
    if (buffer_len == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    zx_status_t status, io_status;
    io_status = fuchsia_device_ControllerGetTopologicalPath(device_.get(),
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
    zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(device_.get(),
                                                                &status, out_info);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::BlockAttachVmo(zx::vmo vmo,
                                              fuchsia_hardware_block_VmoID* out_vmoid) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_BlockAttachVmo(device_.get(), vmo.release(),
                                                      &status, out_vmoid);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeQuery(
        fuchsia_hardware_block_volume_VolumeInfo* out_info) const {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeQuery(device_.get(), &status,
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
        device_.get(), slices, slices_count, &status, out_ranges, out_ranges_count);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeExtend(uint64_t offset, uint64_t length) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeExtend(device_.get(), offset,
                                                           length, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::VolumeShrink(uint64_t offset, uint64_t length) {
    zx_status_t status, io_status;
    io_status = fuchsia_hardware_block_volume_VolumeShrink(device_.get(), offset,
                                                           length, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t RemoteBlockDevice::Create(zx::channel device,
                                      std::unique_ptr<RemoteBlockDevice>* out) {
    zx::fifo fifo;
    zx_status_t status = BlockGetFifo(device, &fifo);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Could not acquire block fifo: %d\n", status);
        return status;
    }
    block_client::Client fifo_client;
    status = block_client::Client::Create(std::move(fifo), &fifo_client);
    if (status != ZX_OK) {
        return status;
    }
    *out = std::unique_ptr<RemoteBlockDevice>(new RemoteBlockDevice(std::move(device),
                                                                    std::move(fifo_client)));
    return ZX_OK;
}

RemoteBlockDevice::RemoteBlockDevice(zx::channel device, block_client::Client fifo_client)
    : device_(std::move(device)), fifo_client_(std::move(fifo_client)) {}

RemoteBlockDevice::~RemoteBlockDevice() {
    BlockCloseFifo(device_);
}

} // namespace blobfs
