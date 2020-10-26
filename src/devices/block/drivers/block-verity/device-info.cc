// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/device-info.h"

#include <zircon/assert.h>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/extra.h"

namespace block_verity {

DeviceInfo DeviceInfo::CreateFromDevice(zx_device_t* device) {
  ddk::BlockProtocolClient block_protocol(device);
  block_info_t blk;
  uint64_t upstream_op_size;
  block_protocol.Query(&blk, &upstream_op_size);
  ZX_ASSERT_MSG(kBlockSize % blk.block_size == 0,
                "underlying block size must evenly divide virtual block size");
  uint32_t hw_blocks_per_virtual_block = kBlockSize / blk.block_size;
  uint64_t virtual_block_count = blk.block_count / hw_blocks_per_virtual_block;
  Geometry geometry(kBlockSize, kHashOutputSize, virtual_block_count);
  uint64_t op_size = upstream_op_size + sizeof(extra_op_t);
  return DeviceInfo(device, geometry, upstream_op_size, op_size, hw_blocks_per_virtual_block);
}

DeviceInfo::DeviceInfo(zx_device_t* device, Geometry geometry_in, uint64_t upstream_op_size_in,
                       uint64_t op_size_in, uint32_t hw_blocks_per_virtual_block_in)
    : block_protocol(device),
      block_device(device),
      geometry(geometry_in),
      upstream_op_size(upstream_op_size_in),
      op_size(op_size_in),
      hw_blocks_per_virtual_block(hw_blocks_per_virtual_block_in) {}

DeviceInfo::DeviceInfo(DeviceInfo&& other)
    : block_protocol(other.block_device),
      block_device(other.block_device),
      geometry(other.geometry),
      upstream_op_size(other.upstream_op_size),
      op_size(other.op_size),
      hw_blocks_per_virtual_block(other.hw_blocks_per_virtual_block) {
  other.block_protocol.clear();
  other.block_device = nullptr;
  other.upstream_op_size = 0;
  other.op_size = 0;
  other.hw_blocks_per_virtual_block = 0;
}

bool DeviceInfo::IsValid() const { return block_protocol.is_valid(); }

}  // namespace block_verity
