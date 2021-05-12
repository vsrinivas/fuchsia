// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/zx/event.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "data_structs.h"
#include "definitions.h"
#include "device_interface.h"

namespace network::internal {

class Session;

class TxQueue {
 public:
  class SessionTransaction;

  static zx::status<std::unique_ptr<TxQueue>> Create(DeviceInterface* parent);
  ~TxQueue() = default;

  // Helper functions with TA annotations that bridges the gap between parent's locks and local
  // locking requirements; TA is not otherwise able to tell that the |parent| and |parent_| are the
  // same entity.
  void AssertParentTxLocked(DeviceInterface& parent) __TA_REQUIRES(parent.tx_lock())
      __TA_ASSERT(parent_->tx_lock()) {
    ZX_DEBUG_ASSERT(parent_ == &parent);
  }
  void AssertParentTxBuffersLocked(DeviceInterface& parent) __TA_REQUIRES(parent.tx_buffers_lock())
      __TA_ASSERT(parent_->tx_buffers_lock()) {
    ZX_DEBUG_ASSERT(parent_ == &parent);
  }

  // Helper class to handle Tx transactions from sessions.
  class SessionTransaction {
   public:
    SessionTransaction(TxQueue* parent, Session* session)
        __TA_ACQUIRE(queue_->parent_->tx_lock(), queue_->parent_->tx_buffers_lock());
    ~SessionTransaction()
        __TA_RELEASE(queue_->parent_->tx_lock(), queue_->parent_->tx_buffers_lock());

    uint32_t available() const { return available_; }
    bool overrun() const { return available_ == 0; }
    tx_buffer_t* GetBuffer() __TA_REQUIRES(queue_->parent_->tx_buffers_lock());
    void Push(uint16_t descriptor)
        __TA_REQUIRES(queue_->parent_->tx_lock(), queue_->parent_->tx_buffers_lock());

    void AssertParentTxLock(DeviceInterface& parent) __TA_ASSERT(parent.tx_lock()) {
      ZX_DEBUG_ASSERT(&parent == queue_->parent_);
    }

   private:
    // Pointer to queue over which transaction is opened, not owned.
    TxQueue* const queue_;
    // Pointer to session that opened the transaction, not owned.
    Session* const session_;
    uint32_t available_;
    uint32_t queued_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(SessionTransaction);
  };

  // Marks all buffers in tx as complete, returning them to their respective sessions.
  void CompleteTxList(const tx_result_t* tx, size_t count) __TA_EXCLUDES(parent_->tx_lock());

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
  uint32_t Enqueue(Session* session, uint16_t descriptor) __TA_REQUIRES(parent_->tx_lock());
  // Returns all outstanding completed buffers to their respective sessions.
  //
  // Returns true if device buffers were full and sessions should be notified.
  [[nodiscard]] bool ReturnBuffers() __TA_REQUIRES(parent_->tx_lock());

  // pointer to parent device, not owned.
  DeviceInterface* const parent_;

  std::unique_ptr<RingQueue<uint32_t>> return_queue_ __TA_GUARDED(parent_->tx_lock());
  std::unique_ptr<IndexedSlab<InFlightBuffer>> in_flight_ __TA_GUARDED(parent_->tx_lock());

  // pre-allocated buffers that are locked to only allow a single session thread to send tx buffers
  // to the device at once.
  std::unique_ptr<tx_buffer_t[]> tx_buffers_ __TA_GUARDED(parent_->tx_buffers_lock());
  std::unique_ptr<BufferParts[]> buffer_parts_ __TA_GUARDED(parent_->tx_buffers_lock());

  DISALLOW_COPY_ASSIGN_AND_MOVE(TxQueue);
};
}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
