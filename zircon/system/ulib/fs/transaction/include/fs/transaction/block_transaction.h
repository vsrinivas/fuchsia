// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_BLOCK_TRANSACTION_H_
#define FS_TRANSACTION_BLOCK_TRANSACTION_H_

#include <zircon/assert.h>
#include <zircon/device/block.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/vector.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>

#ifdef __Fuchsia__
#include <block-client/cpp/block-device.h>
#endif

namespace fs {

// Access the "blkno"-th block within data.
// "blkno = 0" corresponds to the first block within data.
inline void* GetBlock(uint64_t block_size, const void* data, uint64_t blkno) {
  ZX_ASSERT(block_size <= (blkno + 1) * block_size);  // Avoid overflow
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(data) +
                                 static_cast<uintptr_t>(block_size * blkno));
}

// Enqueue multiple writes (or reads) to the underlying block device
// by shoving them into a simple array, to avoid duplicated ops
// within a single operation.
//
// TODO(smklein): This obviously has plenty of room for
// improvement, including:
// - Sorting blocks, combining ranges
// - Writing from multiple buffers (instead of one)
// - Cross-operation writeback delays
class BlockTxn;

// TransactionHandler defines the interface that must be fulfilled
// for an entity to issue transactions to the underlying device.
class TransactionHandler {
 public:
  virtual ~TransactionHandler() {}

  // Acquire the block size of the mounted filesystem.
  // It is assumed that all inputs to the TransactionHandler
  // interface are in |FsBlockSize()|-sized blocks.
  // TODO(rvargas): Remove this method.
  virtual uint32_t FsBlockSize() const = 0;

  // Translates a filesystem-level block number to a block-device-level block number.
  virtual uint64_t BlockNumberToDevice(uint64_t block_num) const = 0;

  // Runs the provided operation against the backing block device. |buffer| provides access to the
  // memory buffer that is referenced by |operation|.
  // The values inside |operation| are expected to be filesystem-level block numbers.
  // This method blocks until the operation completes, so it is suitable for host-based reads and
  // writes and for simple Fuchsia-based reads. Regular Fuchsia IO is expected to be issued against
  // the FIFO exposed through GetDevice().
  virtual zx_status_t RunOperation(const storage::Operation& operation,
                                   storage::BlockBuffer* buffer) = 0;

#ifdef __Fuchsia__
  // Acquires the block group on which the transaction should be issued.
  virtual groupid_t BlockGroupID() = 0;

  // Acquires the block size of the underlying device.
  // TODO(rvargas): Remove this method.
  virtual uint32_t DeviceBlockSize() const = 0;

  // Returns the backing block device that is associated with this TransactionHandler.
  virtual block_client::BlockDevice* GetDevice() = 0;

  // Issues a group of requests to the underlying device and waits
  // for them to complete.
  // TODO(rvargas): Remove this method.
  virtual zx_status_t Transaction(block_fifo_request_t* requests, size_t count) = 0;
#else
  // Reads block |bno| from the device into the buffer provided by |data|.
  // TODO(rvargas): Remove this method.
  virtual zx_status_t Readblk(uint32_t bno, void* data) = 0;

  // Writes block |bno| from the buffer provided by |data| to the device.
  // TODO(rvargas): Remove this method.
  virtual zx_status_t Writeblk(uint32_t bno, const void* data) = 0;
#endif
};

#ifdef __Fuchsia__

class BlockTxn {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
  explicit BlockTxn(TransactionHandler* handler);
  ~BlockTxn();

  // Identify that an operation should be committed to disk
  // at a later point in time.
  void EnqueueOperation(uint32_t op, vmoid_t id, uint64_t vmo_offset, uint64_t dev_offset,
                        uint64_t nblocks);

  // Activate the transaction.
  zx_status_t Transact();

 private:
  TransactionHandler* handler_;
  fbl::Vector<block_fifo_request_t> requests_;
};

#else

// To simplify host-side requests, they are written
// through immediately, and cannot be buffered.
class BlockTxn {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlockTxn);
  explicit BlockTxn(TransactionHandler* handler);
  ~BlockTxn();

  // Identify that an operation should be committed to disk
  // at a later point in time.
  void EnqueueOperation(uint32_t op, const void* id, uint64_t vmo_offset, uint64_t dev_offset,
                        uint64_t nblocks);

  // Activate the transaction (do nothing)
  zx_status_t Transact();

 private:
  TransactionHandler* handler_;
};

#endif

// Provides a type-safe, low-cost abstraction over the |BlockTxn| class,
// allowing clients to avoid intermingling distinct operation types
// unless explicitly requested.
template <typename IdType, uint32_t operation>
class TypedTxn {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TypedTxn);
  explicit TypedTxn(TransactionHandler* handler) : txn_(handler) {}

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

#ifdef __Fuchsia__

using WriteTxn = TypedTxn<vmoid_t, BLOCKIO_WRITE>;
using ReadTxn = TypedTxn<vmoid_t, BLOCKIO_READ>;

#else

using WriteTxn = TypedTxn<const void*, BLOCKIO_WRITE>;
using ReadTxn = TypedTxn<const void*, BLOCKIO_READ>;

#endif

}  // namespace fs

#endif  // FS_TRANSACTION_BLOCK_TRANSACTION_H_
