// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <minfs/writeback.h>

namespace minfs {

// In-memory data buffer.
// This class is thread-compatible.
class Buffer {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Buffer);

  ~Buffer();

  // Initializes the buffer VMO with |blocks| blocks of size kMinfsBlockSize.
  static zx_status_t Create(Bcache* bc, const blk_t blocks, const char* label,
                            std::unique_ptr<Buffer>* out);

  // Returns true if there is space available for |blocks| blocks within the buffer.
  bool IsSpaceAvailable(blk_t blocks) const;

  // Copies a write transaction to the buffer.
  // Also updates the in-memory offsets of the WriteTxn's requests so they point
  // to the correct offsets in the in-memory buffer instead of their original VMOs.
  //
  // |IsSpaceAvailable| should be called before invoking this function to
  // safely guarantee that space exists within the buffer.
  void CopyTransaction(WriteTxn* txn);

  // Returns true if |txn| belongs to this buffer, and if so verifies
  // that it owns the next valid set of blocks within the buffer.
  bool VerifyTransaction(WriteTxn* txn) const;

  // Free the first |blocks| blocks in the buffer.
  void FreeSpace(blk_t blocks);

  blk_t start() const { return start_; }
  blk_t length() const { return length_; }
  blk_t capacity() const { return capacity_; }

 private:
  Buffer(Bcache* bc, fzl::OwnedVmoMapper mapper)
      : bc_(bc),
        mapper_(std::move(mapper)),
        start_(0),
        length_(0),
        capacity_(static_cast<blk_t>(mapper_.size() / kMinfsBlockSize)) {}

  // Returns a pointer to data starting at block |index| in the buffer.
  void* GetData(blk_t index);

  Bcache* bc_;

  fzl::OwnedVmoMapper mapper_;
  fuchsia_hardware_block_VmoID vmoid_ = {.id = VMOID_INVALID};

  blk_t start_ = 0;
  blk_t length_ = 0;
  const blk_t capacity_;
};

enum class WritebackState {
  kInit,      // Initial state of a writeback queue.
  kRunning,   // Indicates that the queue's async processor is currently running.
  kReadOnly,  // State of a writeback queue which no longer allows writes.
};

// Manages an in-memory writeback buffer (and background thread,
// which flushes this buffer out to disk).
class WritebackQueue {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(WritebackQueue);

  ~WritebackQueue();

  // Initializes the WritebackQueue at |out|
  // with a buffer of |buffer_blocks| blocks of size kMinfsBlockSize.
  static zx_status_t Create(Bcache* bc, const blk_t buffer_blocks,
                            fbl::unique_ptr<WritebackQueue>* out);

  // Copies all transaction data referenced from |work| into the writeback buffer.
  zx_status_t Enqueue(fbl::unique_ptr<WritebackWork> work);

  size_t GetCapacity() const { return buffer_->capacity(); }

 private:
  // The waiter struct may be used as a stack-allocated queue for producers.
  // It allows them to take turns putting data into the buffer when it is
  // mostly full.
  struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
  using ProducerQueue = fs::Queue<Waiter*>;
  using WorkQueue = fs::Queue<fbl::unique_ptr<WritebackWork>>;

  WritebackQueue(std::unique_ptr<Buffer> buffer) : buffer_(std::move(buffer)) {}

  bool IsReadOnlyLocked() const __TA_REQUIRES(lock_) { return state_ == WritebackState::kReadOnly; }

  // Blocks until |blocks| blocks of data are free for the caller.
  // Doesn't actually allocate any space.
  void EnsureSpaceLocked(blk_t blocks) __TA_REQUIRES(lock_);

  static int WritebackThread(void* arg);

  // Asynchronously processes writeback work.
  void ProcessLoop();

  // Signalled when the writeback buffer has space to add txns.
  cnd_t work_completed_ = CND_INIT;
  // Signalled when the writeback buffer can be consumed by the background thread.
  cnd_t work_added_ = CND_INIT;

  // Work associated with the "writeback" thread, which manages work items,
  // and flushes them to disk. This thread acts as a consumer of the
  // writeback buffer.
  thrd_t worker_;

  // Use to lock resources that may be accessed asynchronously.
  fbl::Mutex lock_;

  // Buffer which stores transactions to be written out to disk.
  std::unique_ptr<Buffer> buffer_;

  bool unmounting_ __TA_GUARDED(lock_) = false;

  // The WritebackQueue will start off in a kInit state, and will change to kRunning when the
  // background thread is brought up. Once it is running, if an error is detected during
  // writeback, the queue is converted to kReadOnly, and no further writes are permitted.
  WritebackState state_ __TA_GUARDED(lock_) = WritebackState::kInit;

  // Tracks all the pending Writeback Work operations which exist in the
  // writeback buffer and are ready to be sent to disk.
  WorkQueue work_queue_ __TA_GUARDED(lock_){};

  // Ensures that if multiple producers are waiting for space to write their
  // transactions into the writeback buffer, they can each write in-order.
  ProducerQueue producer_queue_ __TA_GUARDED(lock_);
};

}  // namespace minfs
