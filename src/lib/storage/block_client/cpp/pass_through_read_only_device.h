// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace block_client {

// This class is currently for testing only as it will assert if it detects any attempts to write to
// the device.
class PassThroughReadOnlyBlockDevice : public BlockDevice {
 public:
  explicit PassThroughReadOnlyBlockDevice(block_client::BlockDevice* device) : device_(*device) {}

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) override {
    ZX_ASSERT(requests[0].opcode != BLOCKIO_WRITE && requests[0].opcode != BLOCKIO_TRIM);
    return device_.FifoTransaction(requests, count);
  }

  zx::result<std::string> GetDevicePath() const override { return device_.GetDevicePath(); }

  zx_status_t BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const override {
    return device_.BlockGetInfo(out_info);
  }

  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) override {
    return device_.BlockAttachVmo(vmo, out);
  }

  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const override {
    return device_.VolumeGetInfo(out_manager_info, out_volume_info);
  }

  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    return device_.VolumeQuerySlices(slices, slices_count, out_ranges, out_ranges_count);
  }

  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) override { ZX_ASSERT(false); }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override { ZX_ASSERT(false); }

 private:
  block_client::BlockDevice& device_;
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_
