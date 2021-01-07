// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/extra.h"

#include <fuchsia/hardware/block/c/banjo.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/debug.h>

#include "src/devices/block/drivers/block-verity/debug.h"

namespace block_verity {

zx_status_t extra_op_t::Init(block_op_t* block, block_impl_queue_callback cb, void* _cookie,
                             size_t hw_blocks_per_logical_block,
                             size_t data_start_offset_logical_blocks) {
  LOG_ENTRY_ARGS("block=%p, data_start_offset_blocks=%zu", block, data_start_offset_logical_blocks);

  list_initialize(&node);
  completion_cb = cb;
  cookie = _cookie;

  switch (block->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
      vmo = block->rw.vmo;
      length = block->rw.length;
      offset_dev = block->rw.offset_dev;
      offset_vmo = block->rw.offset_vmo;
      // Add the data start offset to offset_dev.
      if (add_overflow(block->rw.offset_dev, data_start_offset_logical_blocks,
                       &block->rw.offset_dev)) {
        zxlogf(ERROR, "adjusted offset overflow: block->rw.offset_dev=%" PRIu64 "",
               block->rw.offset_dev);
        return ZX_ERR_OUT_OF_RANGE;
      }
      // Translate from logical blocks to physical by multiplying length,
      // offset_dev, and offset_vmo by the logical-block multiplier factor.
      if (mul_overflow(block->rw.length, hw_blocks_per_logical_block, &block->rw.length)) {
        zxlogf(ERROR, "adjusted length overflow: block->rw.length=%" PRIu32 "", block->rw.length);
        return ZX_ERR_OUT_OF_RANGE;
      }
      if (mul_overflow(block->rw.offset_dev, hw_blocks_per_logical_block, &block->rw.offset_dev)) {
        zxlogf(ERROR, "adjusted offset overflow: block->rw.offset_dev=%" PRIu64 "",
               block->rw.offset_dev);
        return ZX_ERR_OUT_OF_RANGE;
      }
      if (mul_overflow(block->rw.offset_vmo, hw_blocks_per_logical_block, &block->rw.offset_vmo)) {
        zxlogf(ERROR, "adjusted offset overflow: block->rw.offset_vmo=%" PRIu64 "",
               block->rw.offset_vmo);
        return ZX_ERR_OUT_OF_RANGE;
      }
      break;

    case BLOCK_OP_FLUSH:
      length = 0;
      offset_dev = 0;
      offset_vmo = 0;
      break;

    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

extra_op_t* BlockToExtra(block_op_t* block, size_t op_size) {
  LOG_ENTRY_ARGS("block=%p, op_size=%zu", block, op_size);
  ZX_DEBUG_ASSERT(block);

  uint8_t* ptr = reinterpret_cast<uint8_t*>(block);

  return reinterpret_cast<extra_op_t*>(ptr + op_size) - 1;
}

block_op_t* ExtraToBlock(extra_op_t* extra, size_t op_size) {
  LOG_ENTRY_ARGS("extra=%p, op_size=%zu", extra, op_size);
  ZX_DEBUG_ASSERT(extra);

  uint8_t* ptr = reinterpret_cast<uint8_t*>(extra + 1);

  return reinterpret_cast<block_op_t*>(ptr - op_size);
}

}  // namespace block_verity
