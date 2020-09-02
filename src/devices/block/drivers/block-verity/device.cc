// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/device.h"

#include <zircon/status.h>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/device-info.h"
#include "src/devices/block/drivers/block-verity/extra.h"

namespace block_verity {

// Implementation of the `mutable` read-write block device that does little more
// than translate inbound block reads and writes to the appropriate block offset
// in the underlying device, based on the block allocation.
Device::Device(zx_device_t* parent, DeviceInfo&& info)
    : DeviceType(parent), info_(std::move(info)) {
  zxlogf(INFO, "mutable constructor");
}

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
  zxlogf(INFO, "mutable DdkGetProtocol");
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
  zx_off_t data_size;
  if (mul_overflow(info_.geometry.block_size_, info_.geometry.allocation_.data_block_count,
                   &data_size)) {
    zxlogf(ERROR, "overflowed when computing device size");
    return 0;
  }

  return data_size;
}

void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  zxlogf(INFO, "mutable DdkUnbindNew");
  txn.Reply();
}

void Device::DdkRelease() {
  zxlogf(INFO, "mutable DdkRelease");
  delete this;
}

void Device::BlockImplQuery(block_info_t* out_info, size_t* out_op_size) {
  zxlogf(INFO, "mutable BlockImplQuery");

  info_.block_protocol.Query(out_info, out_op_size);
  // Overwrite block_count with just the number of blocks we're exposing as data
  // blocks.  We keep the superblock & integrity blocks to ourselves.
  // Besides block count and the op size, we're happy to pass through all values
  // from the underlying block device here.
  out_info->block_count = info_.geometry.allocation_.data_block_count;
  out_info->block_size = kBlockSize;
  *out_op_size = info_.op_size;
}

void Device::BlockImplQueue(block_op_t* block_op, block_impl_queue_callback completion_cb,
                            void* cookie) {
  fbl::AutoLock lock(&mtx_);
  extra_op_t* extra = BlockToExtra(block_op, info_.op_size);
  // Save original values in extra, and adjust block_op's block/vmo offsets.
  uint64_t data_start_offset = info_.geometry.AbsoluteLocationForData(0);
  zx_status_t rc = extra->Init(block_op, completion_cb, cookie, info_.hw_blocks_per_virtual_block,
                               data_start_offset);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "failed to initialize extra info: %s", zx_status_get_string(rc));
    BlockComplete(block_op, rc);
    return;
  }

  switch (block_op->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
    case BLOCK_OP_FLUSH:
    case BLOCK_OP_TRIM:
      // Queue to backing block device.
      info_.block_protocol.Queue(block_op, BlockCallback, this);
      break;
    default:
      // Unknown block command, not sure if this is safe to pass through
      BlockComplete(block_op, ZX_ERR_NOT_SUPPORTED);
  }
}

void Device::BlockCallback(void* cookie, zx_status_t status, block_op_t* block) {
  // Restore data that may have changed
  Device* device = static_cast<Device*>(cookie);
  extra_op_t* extra = BlockToExtra(block, device->op_size());
  block->rw.vmo = extra->vmo;
  block->rw.length = extra->length;
  block->rw.offset_dev = extra->offset_dev;
  block->rw.offset_vmo = extra->offset_vmo;

  if (status != ZX_OK) {
    zxlogf(DEBUG, "parent device returned %s", zx_status_get_string(status));
    device->BlockComplete(block, status);
    return;
  }

  switch (block->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
    case BLOCK_OP_FLUSH:
    case BLOCK_OP_TRIM:
      device->BlockComplete(block, ZX_OK);
      break;
    default:
      // This should be unreachable -- we should have rejected/completed this in BlockImplQueue.
      device->BlockComplete(block, ZX_ERR_NOT_SUPPORTED);
  }
}

void Device::BlockComplete(block_op_t* block, zx_status_t status) {
  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  // Complete the request.
  extra->completion_cb(extra->cookie, status, block);
}

}  // namespace block_verity
