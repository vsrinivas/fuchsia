// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_LEGACY_TRANSACTION_HANDLER_H_
#define FS_TRANSACTION_LEGACY_TRANSACTION_HANDLER_H_

#include <fs/transaction/device_transaction_handler.h>

namespace fs {

// TODO(fxbug.dev/49392): remove this class.
class LegacyTransactionHandler : public DeviceTransactionHandler {
 public:
  // Acquire the block size of the mounted filesystem.
  // It is assumed that all inputs to the TransactionHandler
  // interface are in |FsBlockSize()|-sized blocks.
  virtual uint32_t FsBlockSize() const = 0;

  // Acquires the block size of the underlying device.
  virtual uint32_t DeviceBlockSize() const = 0;

  // Issues a group of requests to the underlying device and waits
  // for them to complete.
  virtual zx_status_t Transaction(block_fifo_request_t* requests, size_t count) = 0;
};

// Enqueue multiple writes (or reads) to the underlying block device
// by shoving them into a simple array, to avoid duplicated ops
// within a single operation.
//
// TODO(smklein): This obviously has plenty of room for
// improvement, including:
// - Sorting blocks, combining ranges
// - Writing from multiple buffers (instead of one)
// - Cross-operation writeback delays
class BlockTxn {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
  explicit BlockTxn(LegacyTransactionHandler* handler);
  ~BlockTxn();

  // Identify that an operation should be committed to disk
  // at a later point in time.
  void EnqueueOperation(uint32_t op, vmoid_t id, uint64_t vmo_offset, uint64_t dev_offset,
                        uint64_t nblocks);

  // Activate the transaction.
  zx_status_t Transact();

 private:
  LegacyTransactionHandler* handler_;
  fbl::Vector<block_fifo_request_t> requests_;
};

// Provides a type-safe, low-cost abstraction over the |BlockTxn| class,
// allowing clients to avoid intermingling distinct operation types
// unless explicitly requested.
template <typename IdType, uint32_t operation>
class TypedTxn {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TypedTxn);
  explicit TypedTxn(LegacyTransactionHandler* handler) : txn_(handler) {}

  inline void Enqueue(IdType id, uint64_t vmo_offset, uint64_t dev_offset, uint64_t nblocks) {
    txn_.EnqueueOperation(operation, id, vmo_offset, dev_offset, nblocks);
  }

  inline void EnqueueFlush() {
    IdType id = 0;
    uint64_t vmo_offset = 0;
    uint64_t dev_offset = 0;
    uint64_t nblocks = 0;
    txn_.EnqueueOperation(BLOCKIO_FLUSH, id, vmo_offset, dev_offset, nblocks);
  }

  inline zx_status_t Transact() { return txn_.Transact(); }

 private:
  BlockTxn txn_;
};

using WriteTxn = TypedTxn<vmoid_t, BLOCKIO_WRITE>;
using ReadTxn = TypedTxn<vmoid_t, BLOCKIO_READ>;

}  // namespace fs

#endif  // FS_TRANSACTION_DEVICE_TRANSACTION_HANDLER_H_
