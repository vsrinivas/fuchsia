// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_

#include <lib/zx/event.h>

#include <ddktl/protocol/network/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "data_structs.h"
#include "definitions.h"

namespace network::internal {

class DeviceInterface;
class Session;

class TxQueue {
 public:
  class SessionTransaction;

  static zx_status_t Create(DeviceInterface* parent, std::unique_ptr<TxQueue>* out);
  ~TxQueue() = default;

  // Reclaims all tx buffers currently held by the device implementation.
  void Reclaim() __TA_REQUIRES(lock_) __TA_REQUIRES(buffers_lock_);
  void Lock() __TA_ACQUIRE(lock_) __TA_ACQUIRE(buffers_lock_) {
    lock_.Acquire();
    buffers_lock_.Acquire();
  }
  void Unlock() __TA_RELEASE(lock_) __TA_RELEASE(buffers_lock_) {
    buffers_lock_.Release();
    lock_.Release();
  }

  // Helper class to handle Tx transactions from sessions.
  class SessionTransaction {
   public:
    SessionTransaction(TxQueue* parent, Session* session) __TA_ACQUIRE(queue_->lock_)
        __TA_ACQUIRE(queue_->buffers_lock_);
    ~SessionTransaction() __TA_RELEASE(queue_->lock_) __TA_RELEASE(queue_->buffers_lock_);

    uint32_t available() const { return available_; }
    bool overrun() const { return available_ == 0; }
    tx_buffer_t* GetBuffer() __TA_REQUIRES(queue_->buffers_lock_);
    void Push(uint16_t descriptor) __TA_REQUIRES(queue_->lock_)
        __TA_REQUIRES(queue_->buffers_lock_);

   private:
    // pointer to queue over which transaction is opened, not owned.
    TxQueue* queue_;
    // pointer to session that opened the transaction, not owned.
    Session* session_;
    uint32_t available_;
    uint32_t queued_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(SessionTransaction);
  };

  // Marks all buffers in tx as complete, returning them to their respective sessions.
  void CompleteTxList(const tx_result_t* tx, size_t count) __TA_EXCLUDES(lock_);

 private:
  explicit TxQueue(DeviceInterface* parent) : parent_(parent) {}

  struct InFlightBuffer {
    InFlightBuffer() = default;
    InFlightBuffer(Session* session, zx_status_t result, uint16_t descriptor_index)
        : session(session), result(result), descriptor_index(descriptor_index) {}
    Session* session;
    zx_status_t result;
    uint16_t descriptor_index;
  };

  // Adds the provided session:descriptor tuple to the queue and returns the buffer id.
  uint32_t Enqueue(Session* session, uint16_t descriptor) __TA_REQUIRES(lock_);
  // Marks the buffer with id as complete with the given status.
  void MarkComplete(uint32_t id, zx_status_t status) __TA_REQUIRES(lock_);
  // Returns all outstanding completed buffers to their respective sessions.
  void ReturnBuffers() __TA_REQUIRES(lock_);

  // pointer to parent device, not owned.
  DeviceInterface* parent_;

  fbl::Mutex lock_;
  std::unique_ptr<RingQueue<uint32_t>> return_queue_ __TA_GUARDED(lock_);
  std::unique_ptr<IndexedSlab<InFlightBuffer>> in_flight_ __TA_GUARDED(lock_);

  // pre-allocated buffers that are locked to only allow a single session thread to send tx buffers
  // to the device at once.
  fbl::Mutex buffers_lock_ __TA_ACQUIRED_AFTER(lock_);
  std::unique_ptr<tx_buffer_t[]> tx_buffers_ __TA_GUARDED(buffers_lock_);
  std::unique_ptr<VirtualMemParts[]> virtual_mem_parts_ __TA_GUARDED(buffers_lock_);
  std::unique_ptr<PhysicalMemParts[]> physical_mem_parts_ __TA_GUARDED(buffers_lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(TxQueue);
};
}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
