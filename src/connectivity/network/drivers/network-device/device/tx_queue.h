// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/zx/port.h>
#include <threads.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "data_structs.h"
#include "definitions.h"
#include "device_interface.h"
#include "src/lib/vmo_store/growable_slab.h"

namespace network::internal {

class Session;

class TxQueue {
 public:
  static zx::result<std::unique_ptr<TxQueue>> Create(DeviceInterface* parent);
  using SessionKey = size_t;

  ~TxQueue();
  // Terminates and join the |TxQueue| worker thread.
  void JoinThread();

  // Notifies the worker thread that new tx buffers are available.
  void Resume();

  // Helper functions with TA annotations that bridges the gap between parent's locks and local
  // locking requirements; TA is not otherwise able to tell that the |parent| and |parent_| are the
  // same entity.
  void AssertParentTxLocked(DeviceInterface& parent) __TA_REQUIRES(parent.tx_lock())
      __TA_ASSERT(parent_->tx_lock()) {
    ZX_DEBUG_ASSERT(parent_ == &parent);
  }

  // Adds a session to the Tx queue. The session's tx fifo will be observed and
  // the session is notified when data is available to be fetched and sent to
  // the device through |Session::FetchTx|.
  SessionKey AddSession(Session* session) __TA_REQUIRES(parent_->tx_lock());
  // Removes a session with previously assigned |key|. Panics if |key| is
  // invalid or points to a not installed session.
  void RemoveSession(SessionKey key) __TA_REQUIRES(parent_->tx_lock());

  // Helper class to handle Tx transactions from sessions.
  class SessionTransaction {
   public:
    SessionTransaction(cpp20::span<tx_buffer_t> buffers, TxQueue* parent, Session* session)
        __TA_REQUIRES(parent->parent_->tx_lock());
    void Commit() __TA_EXCLUDES(queue_->parent_->tx_lock());

    uint32_t available() const { return available_; }
    bool overrun() const { return available_ == 0; }
    tx_buffer_t* GetBuffer();
    void Push(uint16_t descriptor) __TA_REQUIRES(queue_->parent_->tx_lock());

    void AssertParentTxLock(DeviceInterface& parent) __TA_ASSERT(queue_->parent_->tx_lock())
        __TA_REQUIRES(parent.tx_lock()) {
      ZX_DEBUG_ASSERT(&parent == queue_->parent_);
    }

   private:
    cpp20::span<tx_buffer_t> buffers_;
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

  void Thread(cpp20::span<tx_buffer_t> buffers);
  zx_status_t EnqueueUserPacket(uint64_t key);
  zx_status_t UpdateFifoWatches();
  zx_status_t HandleFifoSignal(cpp20::span<tx_buffer_t> buffers, SessionKey session,
                               zx_signals_t signals);

  struct InFlightBuffer {
    InFlightBuffer() = default;
    InFlightBuffer(Session* session, zx_status_t result, uint16_t descriptor_index)
        : session(session), result(result), descriptor_index(descriptor_index) {}
    Session* session;
    zx_status_t result;
    uint16_t descriptor_index;
  };

  // Wrapper struct to keep track of ongoing sessions with associated
  // information on whether we have an async wait installed on |port_| for tx
  // FIFOs.
  struct SessionWaiter {
    Session* session;
    bool wait_installed;
  };

  // Adds the provided session:descriptor tuple to the queue and returns the buffer id.
  uint32_t Enqueue(Session* session, uint16_t descriptor) __TA_REQUIRES(parent_->tx_lock());
  // Returns all outstanding completed buffers to their respective sessions.
  //
  // Returns true if device buffers were full and sessions should be notified.
  [[nodiscard]] bool ReturnBuffers() __TA_REQUIRES(parent_->tx_lock());

  static constexpr uint64_t kResumeKey = 1;
  static constexpr uint64_t kQuitKey = 2;

  // pointer to parent device, not owned.
  DeviceInterface* const parent_;

  std::unique_ptr<RingQueue<uint32_t>> return_queue_ __TA_GUARDED(parent_->tx_lock());
  std::unique_ptr<IndexedSlab<InFlightBuffer>> in_flight_ __TA_GUARDED(parent_->tx_lock());
  vmo_store::GrowableSlab<SessionWaiter, SessionKey> sessions_ __TA_GUARDED(parent_->tx_lock());

  zx::port port_;
  std::optional<thrd_t> thread_{};
  std::atomic<bool> running_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(TxQueue);
};
}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TX_QUEUE_H_
