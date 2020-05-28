// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_
#define BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_

#include <block-client/cpp/block-device.h>

namespace block_client {

// This class is currently for testing only as it will assert if it detects any attempts to write to
// the device.
class PassThroughReadOnlyBlockDevice : public BlockDevice {
 public:
  PassThroughReadOnlyBlockDevice(block_client::BlockDevice* device)
      : device_(*device) {}

  zx_status_t ReadBlock(uint64_t block_num, uint64_t block_size, void* block) const override {
    return device_.ReadBlock(block_num, block_size, block);
  }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) override {
    ZX_ASSERT(requests[0].opcode != BLOCKIO_WRITE && requests[0].opcode != BLOCKIO_TRIM);
    return device_.FifoTransaction(requests, count);
  }

  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) const override {
    return device_.GetDevicePath(buffer_len, out_name, out_len);
  }

  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const override {
    return device_.BlockGetInfo(out_info);
  }

  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) override {
    return device_.BlockAttachVmo(vmo, out);
  }

  zx_status_t VolumeQuery(fuchsia_hardware_block_volume_VolumeInfo* out_info) const override {
    return device_.VolumeQuery(out_info);
  }

  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    return device_.VolumeQuerySlices(slices, slices_count, out_ranges, out_ranges_count);
  }

  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) override {
    ZX_ASSERT(false);
  }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    ZX_ASSERT(false);
  }

 private:
  block_client::BlockDevice& device_;
};

}  // namespace block_client

#endif  // BLOCK_CLIENT_CPP_PASS_THROUGH_READ_ONLY_DEVICE_H_
