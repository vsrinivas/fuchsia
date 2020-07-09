// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/debug.h>
#include <fbl/auto_lock.h>

#include "device-info.h"

namespace block_verity {

// Implementation of the `mutable` read-write block device that does little more
// than translate inbound block reads and writes to the appropriate block offset
// in the underlying device, based on the block allocation.
Device::Device(zx_device_t* parent, DeviceInfo&& info)
    : DeviceType(parent), info_(std::move(info)) {
  zxlogf(INFO, "mutable constructor\n");
}

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
  zxlogf(INFO, "mutable DdkGetProtocol\n");
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL:
      proto->ops = &block_impl_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_off_t Device::DdkGetSize() {
  zxlogf(INFO, "mutable DdkGetSize\n");
  zx_off_t data_size;
  if (mul_overflow(info_.block_size, info_.block_allocation.data_block_count, &data_size)) {
    zxlogf(ERROR, "overflowed when computing device size\n");
    return 0;
  }

  return data_size;
}

void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  zxlogf(INFO, "mutable DdkUnbindNew\n");
  txn.Reply();
}

void Device::DdkRelease() {
  zxlogf(INFO, "mutable DdkRelease\n");
  delete this;
}

void Device::BlockImplQuery(block_info_t* out_info, size_t* out_op_size) {
  zxlogf(INFO, "mutable BlockImplQuery\n");

  info_.block_protocol.Query(out_info, out_op_size);
  // Overwrite block_count with just the number of blocks we're exposing as data
  // blocks.  We keep the superblock & integrity blocks to ourselves.
  out_info->block_count = info_.block_allocation.data_block_count;

  // TODO: figure out reasonable max_transfer_size
  // TODO: fix up op size to accommodate larger buffer
  // *out_op_size = info_.op_size;
}

void Device::BlockImplQueue(block_op_t* block_op, block_impl_queue_callback completion_cb,
                            void* cookie) {
  fbl::AutoLock lock(&mtx_);
  zxlogf(INFO, "mutable BlockImplQueue\n");
  // TODO: implement
}

}  // namespace block_verity
