// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_CORE_SERVER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_CORE_SERVER_H_

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
#include <fbl/ref_ptr.h>

#include "iobuffer.h"
#include "message-group.h"
#include "message.h"

class Server {
 public:
  // Creates a new Server.
  static zx_status_t Create(ddk::BlockProtocolClient* bp,
                            fzl::fifo<block_fifo_request_t, block_fifo_response_t>* fifo_out,
                            std::unique_ptr<Server>* out);

  ~Server();

  // Starts the Server using the current thread
  zx_status_t Serve() TA_EXCL(server_lock_);
  zx_status_t AttachVmo(zx::vmo vmo, vmoid_t* out) TA_EXCL(server_lock_);

  // Updates the total number of pending txns, possibly signals
  // the queue-draining thread to wake up if they are waiting
  // for all pending operations to complete.
  //
  // Should only be called for transactions which have been placed
  // on (and removed from) in_queue_.
  void TxnEnd();

  // Wrapper around "SendResponse", as a convenience
  // for finishing both one-shot and group-based transactions.
  void FinishTransaction(zx_status_t status, reqid_t reqid, groupid_t group);

  // Send the given response to the client.
  void SendResponse(const block_fifo_response_t& response);

  void Shutdown();

  // Returns true if the server is about to terminate.
  bool WillTerminate() const;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Server);
  Server(ddk::BlockProtocolClient* bp);

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

  zx_status_t FindVmoIdLocked(vmoid_t* out) TA_REQ(server_lock_);

  fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
  block_info_t info_;
  ddk::BlockProtocolClient* bp_;
  size_t block_op_size_;

  // BARRIER_AFTER is implemented by sticking "BARRIER_BEFORE" on the
  // next operation that arrives.
  bool deferred_barrier_before_ = false;
  MessageQueue in_queue_;
  std::atomic<size_t> pending_count_;
  std::atomic<bool> barrier_in_progress_;
  std::unique_ptr<MessageGroup> groups_[MAX_TXN_GROUP_COUNT];

  fbl::Mutex server_lock_;
  fbl::WAVLTree<vmoid_t, fbl::RefPtr<IoBuffer>> tree_ TA_GUARDED(server_lock_);
  vmoid_t last_id_ TA_GUARDED(server_lock_);
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_CORE_SERVER_H_
