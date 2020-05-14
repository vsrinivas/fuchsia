// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <stdio.h>
#include <string.h>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

#include "usb-mass-storage.h"
#define block_op_to_txn(op) containerof(op, Transaction, op)

namespace ums {
zx_status_t UmsBlockDevice::Add() {
  char name[16];
  snprintf(name, sizeof(name), "lun-%03d", parameters_.lun);
  zx_status_t status = DdkAdd(name);
  if (status == ZX_OK) {
    AddRef();
  }
  return status;
}

void UmsBlockDevice::DdkRelease() { __UNUSED bool released = Release(); }

zx_off_t UmsBlockDevice::DdkGetSize() { return parameters_.block_size * parameters_.total_blocks; }

void UmsBlockDevice::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  info_out->block_size = parameters_.block_size;
  info_out->block_count = parameters_.total_blocks;
  info_out->max_transfer_size = static_cast<uint32_t>(parameters_.max_transfer);
  info_out->flags = parameters_.flags;
  *block_op_size_out = sizeof(Transaction);
}

void UmsBlockDevice::BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb,
                                    void* cookie) {
  Transaction* txn = block_op_to_txn(op);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

  switch (op->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      zxlogf(DEBUG, "UMS QUEUE %s %u @%zu (%p)",
             (op->command & BLOCK_OP_MASK) == BLOCK_OP_READ ? "RD" : "WR", op->rw.length,
             op->rw.offset_dev, op);
      break;
    case BLOCK_OP_FLUSH:
      zxlogf(DEBUG, "UMS QUEUE FLUSH (%p)", op);
      break;
    default:
      zxlogf(ERROR, "ums_block_queue: unsupported command %u", op->command);
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, &txn->op);
      return;
  }
  txn->dev = this;
  queue_callback_(txn);
}

}  // namespace ums
