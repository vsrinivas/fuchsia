// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BLOCK_CORE_SERVER_H_
#define ZIRCON_SYSTEM_DEV_BLOCK_CORE_SERVER_H_

#include <lib/fzl/fifo.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <atomic>
#include <new>
#include <utility>

#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include "txn-group.h"

// Represents the mapping of "vmoid --> VMO"
class IoBuffer : public fbl::WAVLTreeContainable<fbl::RefPtr<IoBuffer>>,
                 public fbl::RefCounted<IoBuffer> {
 public:
  vmoid_t GetKey() const { return vmoid_; }

  // TODO(smklein): This function is currently labelled 'hack' since we have
  // no way to ensure that the size of the VMO won't change in between
  // checking it and using it.  This will require a mechanism to "pin" VMO pages.
  // The units of length and vmo_offset is bytes.
  zx_status_t ValidateVmoHack(uint64_t length, uint64_t vmo_offset);

  zx_handle_t vmo() const { return io_vmo_.get(); }

  IoBuffer(zx::vmo vmo, vmoid_t vmoid);
  ~IoBuffer();

 private:
  friend struct TypeWAVLTraits;
  DISALLOW_COPY_ASSIGN_AND_MOVE(IoBuffer);

  const zx::vmo io_vmo_;
  const vmoid_t vmoid_;
};

class BlockServer;
class BlockMessage;

// A single unit of work transmitted to the underlying block layer.
// BlockMessage contains a block_op_t, which is dynamically sized. Therefore, it implements its
// own allocator that takes block_op_size.
class BlockMessage final : public fbl::DoublyLinkedListable<BlockMessage*> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(BlockMessage);
  BlockMessage() {}

  // Overloaded new operator allows variable-sized allocation to match block op size.
  void* operator new(size_t size) = delete;
  void* operator new(size_t size, size_t block_op_size) {
    return calloc(1, size + block_op_size - sizeof(block_op_t));
  }
  void operator delete(void* msg) { free(msg); }

  // Allocate a new, uninitialized BlockMessage whose block_op begins in a memory region that
  // is block_op_size bytes long.
  static zx_status_t Create(size_t block_op_size, fbl::unique_ptr<BlockMessage>* out);

  // Initialize the contents of this from the supplied args. block_op op_ is cleared.
  void Init(fbl::RefPtr<IoBuffer> iobuf, BlockServer* server, block_fifo_request_t* req);

  // End the transaction specified by reqid and group, and release iobuf.
  // BlockMessage can be reused with another call to Init().
  void Complete(zx_status_t status);

  block_op_t* Op() { return &op_; }

 private:
  fbl::RefPtr<IoBuffer> iobuf_;
  BlockServer* server_;
  reqid_t reqid_;
  groupid_t group_;
  size_t op_size_;
  // Must be at the end of structure.
  union {
    block_op_t op_;
    uint8_t _op_raw_[1];  // Extra space for underlying block_op.
  };
};

using BlockMessageQueue = fbl::DoublyLinkedList<BlockMessage*>;

class BlockServer {
 public:
  // Creates a new BlockServer.
  static zx_status_t Create(ddk::BlockProtocolClient* bp,
                            fzl::fifo<block_fifo_request_t, block_fifo_response_t>* fifo_out,
                            BlockServer** out);

  // Starts the BlockServer using the current thread
  zx_status_t Serve() TA_EXCL(server_lock_);
  zx_status_t AttachVmo(zx::vmo vmo, vmoid_t* out) TA_EXCL(server_lock_);

  // Updates the total number of pending txns, possibly signals
  // the queue-draining thread to wake up if they are waiting
  // for all pending operations to complete.
  //
  // Should only be called for transactions which have been placed
  // on (and removed from) in_queue_.
  void TxnEnd();

  // Wrapper around "Completed Transaction", as a convenience
  // both both one-shot and group-based transactions.
  //
  // (If appropriate) tells the client that their operation is done.
  void TxnComplete(zx_status_t status, reqid_t reqid, groupid_t group);

  void ShutDown();
  ~BlockServer();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(BlockServer);
  BlockServer(ddk::BlockProtocolClient* bp);

  // Helper for processing a single message read from the FIFO.
  void ProcessRequest(block_fifo_request_t* request);
  zx_status_t ProcessReadWriteRequest(block_fifo_request_t* request) TA_EXCL(server_lock_);
  zx_status_t ProcessCloseVmoRequest(block_fifo_request_t* request) TA_EXCL(server_lock_);
  zx_status_t ProcessFlushRequest(block_fifo_request_t* request);
  zx_status_t ProcessTrimRequest(block_fifo_request_t* request);

  // Helper for the server to react to a signal that a barrier
  // operation has completed. Unsets the local "waiting for barrier"
  // signal, and enqueues any further operations that might be
  // pending.
  void BarrierComplete();

  // Functions that read from the fifo and invoke the queue drainer.
  // Should not be invoked concurrently.
  zx_status_t Read(block_fifo_request_t* requests, size_t* count);
  void TerminateQueue();

  // Attempts to enqueue all operations on the |in_queue_|. Stops
  // when either the queue is empty, or a BARRIER_BEFORE is reached and
  // operations are in-flight.
  void InQueueDrainer();

  zx_status_t FindVmoIDLocked(vmoid_t* out) TA_REQ(server_lock_);

  fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
  block_info_t info_;
  ddk::BlockProtocolClient* bp_;
  size_t block_op_size_;

  // BARRIER_AFTER is implemented by sticking "BARRIER_BEFORE" on the
  // next operation that arrives.
  bool deferred_barrier_before_ = false;
  BlockMessageQueue in_queue_;
  std::atomic<size_t> pending_count_;
  std::atomic<bool> barrier_in_progress_;
  TransactionGroup groups_[MAX_TXN_GROUP_COUNT];

  fbl::Mutex server_lock_;
  fbl::WAVLTree<vmoid_t, fbl::RefPtr<IoBuffer>> tree_ TA_GUARDED(server_lock_);
  vmoid_t last_id_ TA_GUARDED(server_lock_);
};

#endif  // ZIRCON_SYSTEM_DEV_BLOCK_CORE_SERVER_H_
