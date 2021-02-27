// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/transaction/legacy_transaction_handler.h"

#include <lib/trace/event.h>

#include "trace.h"

namespace fs {

BlockTxn::BlockTxn(LegacyTransactionHandler* handler) : handler_(handler) {}

BlockTxn::~BlockTxn() { Transact(); }

void BlockTxn::EnqueueOperation(uint32_t op, vmoid_t id, uint64_t vmo_offset, uint64_t dev_offset,
                                uint64_t nblocks) {
  // TODO(fxbug.dev/32112): Remove this assertion.
  ZX_ASSERT_MSG(nblocks < UINT32_MAX, "Too many blocks");
  uint32_t blocks = static_cast<uint32_t>(nblocks);
  for (size_t i = 0; i < requests_.size(); i++) {
    if (requests_[i].vmoid != id || requests_[i].opcode != op) {
      continue;
    }

    if (requests_[i].vmo_offset == vmo_offset) {
      // Take the longer of the operations (if operating on the same blocks).
      if (requests_[i].length <= blocks) {
        requests_[i].length = blocks;
      }
      return;
    } else if ((requests_[i].vmo_offset + requests_[i].length == vmo_offset) &&
               (requests_[i].dev_offset + requests_[i].length == dev_offset)) {
      // Combine with the previous request, if immediately following.
      requests_[i].length += blocks;
      return;
    }
  }

  block_fifo_request_t request;
  request.opcode = op;
  request.vmoid = id;
  // NOTE: It's easier to compare everything when dealing with blocks (not offsets!) so the
  // following are described in terms of blocks until we Transact().
  request.length = blocks;
  request.vmo_offset = vmo_offset;
  request.dev_offset = dev_offset;
  request.trace_flow_id = GenerateTraceId();
  requests_.push_back(request);
}

zx_status_t BlockTxn::Transact() {
  // Fast-path for already completed transactions.
  if (requests_.is_empty()) {
    return ZX_OK;
  }
  TRACE_DURATION("storage", "LegacyTransactionHandler::RunRequests", "num", requests_.size());
  // Convert 'filesystem block' units to 'disk block' units.
  const size_t kBlockFactor = handler_->FsBlockSize() / handler_->DeviceBlockSize();
  for (size_t i = 0; i < requests_.size(); i++) {
    requests_[i].vmo_offset *= kBlockFactor;
    requests_[i].dev_offset *= kBlockFactor;
    // TODO(fxbug.dev/32112): Remove this assertion.
    uint64_t length = requests_[i].length * kBlockFactor;
    ZX_ASSERT_MSG(length < UINT32_MAX, "Too many blocks");
    requests_[i].length = static_cast<uint32_t>(length);
  }
  {
    // This duration mainly exists to give the below TRACE_FLOW_BEGIN calls a context which ends
    // before the actual blocking call to |Transaction|. Flow events originate from the end of the
    // duration that they were defined in.
    TRACE_DURATION("storage", "LegacyTransactionHandler::RunRequests::Enqueue");
    for (const auto& request : requests_) {
      TRACE_FLOW_BEGIN("storage", "BlockTransaction", request.trace_flow_id);
    }
  }
  zx_status_t status = handler_->Transaction(requests_.data(), requests_.size());
  TRACE_DURATION("storage", "LegacyTransactionHandler::RunRequests::Finish");
  for (const auto& request : requests_) {
    TRACE_FLOW_END("storage", "BlockTransaction", request.trace_flow_id);
  }
  requests_.reset();
  return status;
}

}  // namespace fs
