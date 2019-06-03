// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <block-client/cpp/client.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>

#include <memory>

namespace blobfs {

// An interface which virtualizes the connection to the underlying block device.
class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    // TODO(ZX-4128): Deprecate this interface. Favor reading over the FIFO
    // protocol instead.
    virtual zx_status_t ReadBlock(uint64_t block_num, uint64_t block_size, void* block) const = 0;

    // FIFO protocol.
    virtual zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) = 0;

    // Controller IPC.
    virtual zx_status_t GetDevicePath(size_t buffer_len, char* out_name,
                                      size_t* out_len) const = 0;

    // Block IPC.
    virtual zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const = 0;
    virtual zx_status_t BlockAttachVmo(zx::vmo vmo, fuchsia_hardware_block_VmoID* out_vmoid) = 0;

    // Volume IPC.
    virtual zx_status_t VolumeQuery(fuchsia_hardware_block_volume_VolumeInfo* out_info) const = 0;
    virtual zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                          fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                          size_t* out_ranges_count) const = 0;
    virtual zx_status_t VolumeExtend(uint64_t offset, uint64_t length) = 0;
    virtual zx_status_t VolumeShrink(uint64_t offset, uint64_t length) = 0;
};

// A concrete implementation of |BlockDevice|.
//
// This class is not movable or copyable.
class RemoteBlockDevice final : public BlockDevice {
public:
    static zx_status_t Create(zx::channel device, std::unique_ptr<RemoteBlockDevice>* out);
    RemoteBlockDevice& operator=(RemoteBlockDevice&&) = delete;
    RemoteBlockDevice(RemoteBlockDevice&&) = delete;
    RemoteBlockDevice& operator=(const RemoteBlockDevice&) = delete;
    RemoteBlockDevice(const RemoteBlockDevice&) = delete;
    ~RemoteBlockDevice();

    zx_status_t ReadBlock(uint64_t block_num, uint64_t block_size, void* block) const final;
    zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;
    zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) const final;
    zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final;
    zx_status_t BlockAttachVmo(zx::vmo vmo, fuchsia_hardware_block_VmoID* out_vmoid) final;
    zx_status_t VolumeQuery(fuchsia_hardware_block_volume_VolumeInfo* out_info) const final;
    zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                  fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                  size_t* out_ranges_count) const final;
    zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final;
    zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final;

private:
    RemoteBlockDevice(zx::channel device, block_client::Client fifo_client);

    zx::channel device_;
    block_client::Client fifo_client_;
};

} // namespace blobfs
