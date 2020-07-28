// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-info.h"

#include "extra.h"

namespace block_verity {

DeviceInfo DeviceInfo::CreateFromDevice(zx_device_t* device) {
  ddk::BlockProtocolClient block_protocol(device);
  block_info_t blk;
  uint64_t upstream_op_size;
  block_protocol.Query(&blk, &upstream_op_size);
  Geometry geometry(blk.block_size, 32, blk.block_count);
  uint64_t op_size = upstream_op_size + sizeof(extra_op_t);
  return DeviceInfo(device, geometry, upstream_op_size, op_size);
}

DeviceInfo::DeviceInfo(zx_device_t* device, Geometry geometry_in, uint64_t upstream_op_size_in,
                       uint64_t op_size_in)
    : block_protocol(device),
      block_device(device),
      geometry(geometry_in),
      upstream_op_size(upstream_op_size_in),
      op_size(op_size_in) {}

DeviceInfo::DeviceInfo(DeviceInfo&& other)
    : block_protocol(other.block_device),
      block_device(other.block_device),
      geometry(other.geometry),
      upstream_op_size(other.upstream_op_size),
      op_size(other.op_size) {
  other.block_protocol.clear();
  other.block_device = nullptr;
  other.upstream_op_size = 0;
  other.op_size = 0;
}

bool DeviceInfo::IsValid() const { return block_protocol.is_valid(); }

}  // namespace block_verity
