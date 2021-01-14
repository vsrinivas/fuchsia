// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oob_doubler.h"

#include <zircon/assert.h>

namespace ftl {

void OobDoubler::Query(nand_info_t* info_out, size_t* nand_op_size_out) {
  parent_.Query(info_out, nand_op_size_out);
  active_ = info_out->oob_size < kThreshold;
  if (active_) {
    info_out->page_size *= 2;
    info_out->oob_size *= 2;
    info_out->pages_per_block /= 2;
  }
}

void OobDoubler::Queue(nand_operation_t* operation, nand_queue_callback completion_cb,
                       void* cookie) {
  if (active_) {
    switch (operation->command) {
      case NAND_OP_READ:
      case NAND_OP_WRITE:
        operation->rw.length *= 2;
        operation->rw.offset_nand *= 2;
        operation->rw.offset_data_vmo *= 2;
        operation->rw.offset_oob_vmo *= 2;
        break;

      case NAND_OP_ERASE:
        break;

      default:
        ZX_DEBUG_ASSERT(false);
    }
  }
  parent_.Queue(operation, completion_cb, cookie);
}

}  // namespace ftl.
