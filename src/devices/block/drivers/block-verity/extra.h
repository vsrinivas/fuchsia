// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_EXTRA_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_EXTRA_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/protocol/block.h>

#include "block-loader-interface.h"

namespace block_verity {

// `extra_op_t` is the extra information placed in the tail end of `block_op_t`s queued against a
// `block_verity::Device`.
//
static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "uintptr_t > uint64_t");
struct extra_op_t {
  // Used to link deferred block requests
  list_node_t node;

  // The remaining are used to save fields of the original block request which may be altered
  zx_handle_t vmo;
  uint32_t length;
  uint64_t offset_dev;
  uint64_t offset_vmo;
  block_impl_queue_callback completion_cb;
  void* cookie;

  // used to save a different type of callback function pointer
  BlockLoaderCallback loader_cb;

  // Resets this structure to an initial state.
  zx_status_t Init(block_op_t* block, block_impl_queue_callback completion_cb, void* cookie,
                   size_t reserved_blocks);
};

// Translates `block_op_t`s to `extra_op_t`s and vice versa.
extra_op_t* BlockToExtra(block_op_t* block, size_t op_size);
block_op_t* ExtraToBlock(extra_op_t* extra, size_t op_size);

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_EXTRA_H_
