// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-info.h"

#include "extra.h"

namespace block_verity {

DeviceInfo::DeviceInfo(zx_device_t* device)
    : block_protocol(device), block_device(device), block_size(0), op_size(0) {
  block_info_t blk;
  block_protocol.Query(&blk, &op_size);
  block_count = blk.block_count;
  block_size = blk.block_size;
  op_size += sizeof(extra_op_t);
  block_allocation = BestSplitFor(blk.block_size, 32, blk.block_count);
}

DeviceInfo::DeviceInfo(DeviceInfo&& other)
    : block_protocol(other.block_device),
      block_device(other.block_device),
      block_size(other.block_size),
      block_count(other.block_count),
      op_size(other.op_size),
      block_allocation(other.block_allocation) {
  other.block_protocol.clear();
  other.block_device = nullptr;
  other.block_size = 0;
  other.block_count = 0;
  other.op_size = 0;
}

bool DeviceInfo::IsValid() const { return block_protocol.is_valid(); }

}  // namespace block_verity
