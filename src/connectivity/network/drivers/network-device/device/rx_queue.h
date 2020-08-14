// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_RX_QUEUE_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_RX_QUEUE_H_

#include <lib/zx/port.h>

#include <ddktl/protocol/network/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "data_structs.h"
#include "definitions.h"

namespace network::internal {

class DeviceInterface;
class Session;

class RxQueue {
 public:
  class DeviceTransaction;

  static zx_status_t Create(DeviceInterface* parent, std::unique_ptr<RxQueue>* out);
  ~RxQueue();

  // Reclaims all buffers currently held by the device.
  void Reclaim() __TA_REQUIRES(lock_);
  void Lock() __TA_ACQUIRE(lock_) { lock_.Acquire(); }
  void Unlock() __TA_RELEASE(lock_) { lock_.Release(); }
  // Drops all queued buffers attributed to the given session, and marks the session as rx-disabled.
  // Called by the DeviceInterface parent when the session is marked as dead.
  void PurgeSession(Session* session);
  // Returns rx buffers to their respective sessions.
  void CompleteRxList(const rx_buffer_t* rx, size_t count) __TA_EXCLUDES(lock_);
  // Notifies watcher thread that the primary session changed.
  void TriggerSessionChanged();
  // Poke watcher thread to try to fetch more rx descriptors.
  void TriggerRxWatch();
  // Kills and joins the watcher thread.
  void JoinThread();

  // A transaction to add buffers from a session to the RxQueue.
  class SessionTransaction {
   public:
    explicit SessionTransaction(RxQueue* parent) __TA_REQUIRES(parent->lock_) : queue_(parent) {}
    ~SessionTransaction() __TA_REQUIRES(queue_->lock_) = default;
    uint32_t remaining();
    void Push(Session* session, uint16_t descriptor);

   private:
    // Pointer to parent queue, not owned.
    RxQueue* queue_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(SessionTransaction);
  };

 private:
  static constexpr uint16_t kDeviceHasBuffer = 0x01;

  explicit RxQueue(DeviceInterface* parent) : parent_(parent) {}

  struct InFlightBuffer {
    InFlightBuffer() = default;
    InFlightBuffer(Session* session, uint16_t descriptor_index)
        : session(session), descriptor_index(descriptor_index), flags(0) {}
    Session* session;
    uint16_t descriptor_index;
    uint16_t flags;
  };
  // Get a single buffer from the queue, along with its identifier. On success, the buffer is popped
  // from the queue. The returned buffer pointer is still owned by the queue and the pointer should
  // not outlive the currently held lock.
  std::tuple<InFlightBuffer*, uint32_t> GetBuffer() __TA_REQUIRES(lock_);
  // Pops a buffer from the queue, if any are available, and stores the space information in `buff`.
  // Returns ZX_ERR_NO_RESOURCES if there are no buffers available.
  zx_status_t PrepareBuff(rx_space_buffer_t* buff) __TA_REQUIRES(lock_);
  // Signals the parent DeviceInterface to commit all outstanding Rx buffers.
  void ReturnBuffers() __TA_REQUIRES(lock_);
  int WatchThread();
  // Reclaims the buffer with `id` from the device. If the buffer's session is still valid, gives it
  // to the session, otherwise drops it.
  void ReclaimBuffer(uint32_t id) __TA_REQUIRES(lock_);

  // pointer to parent device, not owned.
  DeviceInterface* parent_;
  std::unique_ptr<IndexedSlab<InFlightBuffer>> in_flight_ __TA_GUARDED(lock_);
  std::unique_ptr<RingQueue<uint32_t>> available_queue_ __TA_GUARDED(lock_);
  size_t device_buffer_count_ __TA_GUARDED(lock_) = 0;
  fbl::Mutex lock_;

  // there are pre-allocated buffers that are only used by the rx watch thread.
  std::unique_ptr<rx_space_buffer_t[]> space_buffers_;
  std::unique_ptr<BufferParts[]> buffer_parts_;

  zx::port rx_watch_port_;
  fit::optional<thrd_t> rx_watch_thread_{};
  std::atomic<bool> running_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RxQueue);
};

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_RX_QUEUE_H_
