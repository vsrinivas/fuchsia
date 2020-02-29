// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/transaction/block_transaction.h>

#include <zircon/device/block.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/vector.h>

namespace fs {

BlockTxn::BlockTxn(TransactionHandler* handler) : handler_(handler) {}

BlockTxn::~BlockTxn() { Transact(); }

#ifdef __Fuchsia__

zx_status_t TransactionHandler::RunRequests(
    const std::vector<storage::BufferedOperation>& operations) {
  if (operations.empty()) {
    return ZX_OK;
  }

  // Update all the outgoing transactions to be in disk blocks.
  std::vector<block_fifo_request_t> block_requests;
  block_requests.resize(operations.size());
  for (size_t i = 0; i < operations.size(); i++) {
    auto& request = block_requests[i];
    request.group = BlockGroupID();
    request.vmoid = operations[i].vmoid;

    const auto& operation = operations[i].op;
    switch (operation.type) {
      case storage::OperationType::kRead:
        request.opcode = BLOCKIO_READ;
        break;
      case storage::OperationType::kWrite:
        request.opcode = BLOCKIO_WRITE;
        break;
      case storage::OperationType::kTrim:
        request.opcode = BLOCKIO_TRIM;
        break;
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported operation");
    }
    // For the time being, restrict a transaction to operations of the same type.
    // This probably can be relaxed, as the concept of a transaction implies the
    // operations take place logically at the same time, so even if there's a
    // mix of reads and writes, it doesn't make sense to depend on the relative
    // order of the operations, which is what could break with the merging done
    // by the request builder.
    ZX_DEBUG_ASSERT(operation.type == operations[0].op.type);

    request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
    request.dev_offset = BlockNumberToDevice(operation.dev_offset);
    uint64_t length = BlockNumberToDevice(operation.length);
    // TODO(ZX-2253): Requests this long, although unlikely, should be
    // handled more gracefully.
    ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
    request.length = static_cast<uint32_t>(length);
  }

  return GetDevice()->FifoTransaction(&block_requests[0], operations.size());
}

void BlockTxn::EnqueueOperation(uint32_t op, vmoid_t id, uint64_t vmo_offset, uint64_t dev_offset,
                                uint64_t nblocks) {
  // TODO(ZX-2253): Remove this assertion.
  ZX_ASSERT_MSG(nblocks < UINT32_MAX, "Too many blocks");
  uint32_t blocks = static_cast<uint32_t>(nblocks);
  for (size_t i = 0; i < requests_.size(); i++) {
    if (requests_[i].vmoid != id || requests_[i].opcode != op) {
      continue;
    }

    if (requests_[i].vmo_offset == vmo_offset) {
      // Take the longer of the operations (if operating on the same
      // blocks).
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
  request.group = handler_->BlockGroupID();
  request.vmoid = id;
  // NOTE: It's easier to compare everything when dealing
  // with blocks (not offsets!) so the following are described in
  // terms of blocks until we Transact().
  request.length = blocks;
  request.vmo_offset = vmo_offset;
  request.dev_offset = dev_offset;
  requests_.push_back(std::move(request));
}

zx_status_t BlockTxn::Transact() {
  // Fast-path for already completed transactions.
  if (requests_.is_empty()) {
    return ZX_OK;
  }
  // Convert 'filesystem block' units to 'disk block' units.
  const size_t kBlockFactor = handler_->FsBlockSize() / handler_->DeviceBlockSize();
  for (size_t i = 0; i < requests_.size(); i++) {
    requests_[i].vmo_offset *= kBlockFactor;
    requests_[i].dev_offset *= kBlockFactor;
    // TODO(ZX-2253): Remove this assertion.
    uint64_t length = requests_[i].length * kBlockFactor;
    ZX_ASSERT_MSG(length < UINT32_MAX, "Too many blocks");
    requests_[i].length = static_cast<uint32_t>(length);
  }
  zx_status_t status = ZX_OK;
  if (requests_.size() != 0) {
    status = handler_->Transaction(requests_.data(), requests_.size());
  }
  requests_.reset();
  return status;
}

#else

void BlockTxn::EnqueueOperation(uint32_t op, const void* id, uint64_t vmo_offset,
                                uint64_t dev_offset, uint64_t nblocks) {
  for (size_t b = 0; b < nblocks; b++) {
    void* data = GetBlock(handler_->FsBlockSize(), id, vmo_offset + b);
    if (op == BLOCKIO_WRITE) {
      handler_->Writeblk(static_cast<uint32_t>(dev_offset + b), data);
    } else if (op == BLOCKIO_READ) {
      handler_->Readblk(static_cast<uint32_t>(dev_offset + b), data);
    } else if (op == BLOCKIO_FLUSH) {
      // No-op.
    } else {
      ZX_ASSERT(false);  // Invalid operation.
    }
  }
}

// Activate the transaction (do nothing)
zx_status_t BlockTxn::Transact() { return ZX_OK; }

#endif

}  // namespace fs
