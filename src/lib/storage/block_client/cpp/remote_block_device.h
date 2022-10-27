// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_REMOTE_BLOCK_DEVICE_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_REMOTE_BLOCK_DEVICE_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <mutex>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace block_client {

// A concrete implementation of |BlockDevice|.
//
// This class is not movable or copyable.
class RemoteBlockDevice final : public BlockDevice {
 public:
  static zx_status_t Create(zx::channel device, std::unique_ptr<RemoteBlockDevice>* out);
  static zx::result<std::unique_ptr<RemoteBlockDevice>> Create(int fd);
  RemoteBlockDevice& operator=(RemoteBlockDevice&&) = delete;
  RemoteBlockDevice(RemoteBlockDevice&&) = delete;
  RemoteBlockDevice& operator=(const RemoteBlockDevice&) = delete;
  RemoteBlockDevice(const RemoteBlockDevice&) = delete;
  ~RemoteBlockDevice();

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;
  zx::result<std::string> GetDevicePath() const final;
  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final;
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) final;
  zx_status_t VolumeGetInfo(fuchsia_hardware_block_volume_VolumeManagerInfo* out_manager_info,
                            fuchsia_hardware_block_volume_VolumeInfo* out_volume_info) const final;
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const final;
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final;
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final;

 private:
  RemoteBlockDevice(zx::channel device, zx::fifo fifo);

  zx::channel device_;
  block_client::Client fifo_client_;
};

// Helper functions for performing single reads and writes from a block.
// These functions are provided as a drop in replacement for the discontinued
// pread and pwrite posix style calls, and should not be used in new code.
// buffer_size and offset are considered sizes in bytes, although
// reading and writing can only be done in whole block increments.
// buffer must be pre-allocated to the correct size.
zx_status_t SingleReadBytes(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                            void* buffer, size_t buffer_size, size_t offset);
zx_status_t SingleWriteBytes(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                             void* buffer, size_t buffer_size, size_t offset);

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_REMOTE_BLOCK_DEVICE_H_
