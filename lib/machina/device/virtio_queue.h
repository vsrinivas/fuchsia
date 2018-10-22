// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_DEVICE_VIRTIO_QUEUE_H_
#define GARNET_LIB_MACHINA_DEVICE_VIRTIO_QUEUE_H_

#include <mutex>

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include "garnet/lib/machina/device/phys_mem.h"

struct vring_desc;
struct vring_avail;
struct vring_used;

namespace machina {

class VirtioChain;

// Stores the Virtio queue based on the ring provided by the guest.
//
// NOTE(abdulla): This structure points to guest-controlled memory.
struct VirtioRing {
  // Number of entries in the descriptor table.
  uint16_t size;
  uint16_t index;

  const volatile struct vring_desc* desc;  // guest-controlled

  const volatile struct vring_avail* avail;  // guest-controlled
  const volatile uint16_t* used_event;       // guest-controlled

  volatile struct vring_used* used;  // guest-controlled
  volatile uint16_t* avail_event;    // guest-controlled
};

// A higher-level API for vring_desc.
struct VirtioDescriptor {
  // Pointer to the buffer in our address space.
  void* addr;
  // Number of bytes at addr.
  uint32_t len;
  // Only valid if has_next is true.
  // TODO(abdulla): Remove this.
  uint16_t next;
  // Is there another buffer after this one?
  // TODO(abdulla): Remove this.
  bool has_next;
  // If true, this buffer must only be written to (no reads). Otherwise this
  // buffer must only be read from (no writes).
  bool writable;
};

class VirtioQueue {
 public:
  // The signal asserted when there are available descriptors in the queue.
  static constexpr zx_signals_t SIGNAL_QUEUE_AVAIL = ZX_USER_SIGNAL_0;

  VirtioQueue();

  // Sets the guest physical memory for the queue.
  void set_phys_mem(const PhysMem* phys_mem) { phys_mem_ = phys_mem; }

  // Sets the interrupt callback from the queue.
  enum InterruptAction : uint8_t {
    // Set a flag to inspect queues on the next interrupt.
    SET_QUEUE = 1 << 0,
    // Set a flag to inspect configs on the next interrupt.
    SET_CONFIG = 1 << 1,
    // If a flag is set, send an interrupt to the device.
    TRY_INTERRUPT = 1 << 2,
  };
  using InterruptFn = fit::function<zx_status_t(uint8_t actions)>;
  void set_interrupt(InterruptFn fn) { interrupt_ = std::move(fn); }

  // Gets the number of descriptors in the queue.
  uint16_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_.size;
  }

  // If the device negotiates |VIRTIO_F_EVENT_IDX|, this is the number of
  // descriptors to allow the driver to queue into the avail ring before
  // signaling the device that the queue has descriptors.
  //
  // The default value is 1 so that every update to the avail ring causes a
  // notification that descriptors are available.
  //
  // If the device does not negotiate |VIRTIO_F_EVENT_IDX|, this attribute has
  // no effect.
  uint16_t avail_event_num() {
    std::lock_guard<std::mutex> lock(mutex_);
    return avail_event_num_;
  }
  void set_avail_event_num(uint16_t num) {
    std::lock_guard<std::mutex> lock(mutex_);
    avail_event_num_ = num;
  }
  void set_use_event_index(bool use) {
    std::lock_guard<std::mutex> lock(mutex_);
    use_event_index_ = use;
  }

  // Returns a handle that can waited on for available descriptors in the.
  // While buffers are available in the queue |ZX_USER_SIGNAL_0| will be
  // asserted.
  zx_handle_t event() const { return event_.get(); }

  // Configure the queue using a set of addresses, and set the queue size.
  void Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                 zx_gpaddr_t used);

  bool NextChain(VirtioChain* chain);

  // Get the index of the next descriptor in the available ring.
  //
  // If a buffer is a available, the descriptor index is written to |index|, the
  // queue index pointer is incremented, and ZX_OK is returned.
  //
  // If no buffers are available ZX_ERR_SHOULD_WAIT is returned.
  zx_status_t NextAvail(uint16_t* index) {
    std::lock_guard<std::mutex> lock(mutex_);
    return NextAvailLocked(index);
  }

  bool HasAvail() {
    std::lock_guard<std::mutex> lock(mutex_);
    return HasAvailLocked();
  }

  // Notify waiting threads blocked on |virtio_queue_wait| that the avail ring
  // has descriptors available.
  zx_status_t Notify();

  // Return a descriptor to the used ring.
  //
  // |index| must be a value received from a call to virtio_queue_next_avail.
  // Any buffers accessed via |index| or any chained descriptors must not be
  // used after calling virtio_queue_return.
  //
  // The |action| parameter allows the caller to suppress sending an interrupt
  // if (for example) the device is returning several descriptors sequentially.
  // The |SEND_INTERRUPT| flag will still respect any requirements enforced by
  // the bus regarding interrupt suppression.
  zx_status_t Return(uint16_t index, uint32_t len,
                     uint8_t actions = SET_QUEUE | TRY_INTERRUPT);

  // Reads a single descriptor from the queue.
  //
  // This method should only be called using descriptor indices acquired with
  // virtio_queue_next_avail (including any chained descriptors) and before
  // they've been released with virtio_queue_return.
  __WARN_UNUSED_RESULT zx_status_t ReadDesc(uint16_t index,
                                            VirtioDescriptor* desc);

  // Callback for |Poll| and |PollAsync|.
  //
  // queue    - The queue being polled.
  // head     - Descriptor index of the buffer chain to process.
  // used     - The number of bytes written to the descriptor chain must be
  //            written here.
  //
  // The queue will continue to be polled as long as this method returns ZX_OK.
  // The error ZX_ERR_STOP will be treated as a special value to indicate queue
  // polling should stop gracefully and (in the case of |Poll|)terminate the
  // thread.
  //
  // Any other error values will be treated as unexpected errors that will cause
  // the polling thread to be terminated with a non-zero exit value.
  using PollFn = fit::function<zx_status_t(VirtioQueue* queue, uint16_t head,
                                           uint32_t* used)>;

  // Monitors the queue signal for available descriptors and run the callback
  // when one is available.
  zx_status_t PollAsync(async_dispatcher_t* dispatcher, async::Wait* wait,
                        PollFn handler);

 private:
  zx_status_t NextAvailLocked(uint16_t* index) __TA_REQUIRES(mutex_);
  bool HasAvailLocked() const __TA_REQUIRES(mutex_);

  // Returns a circular index into a Virtio ring.
  uint32_t RingIndexLocked(uint32_t index) const __TA_REQUIRES(mutex_);

  void InvokeAsyncHandler(async_dispatcher_t* dispatcher, async::Wait* wait,
                          zx_status_t status, const PollFn& handler);

  mutable std::mutex mutex_;
  const PhysMem* phys_mem_ = nullptr;
  InterruptFn interrupt_;
  VirtioRing ring_ __TA_GUARDED(mutex_) = {};
  zx::event event_;
  uint16_t avail_event_num_ __TA_GUARDED(mutex_) = 1;
  bool use_event_index_ __TA_GUARDED(mutex_) = false;

  friend class VirtioQueueFake;
};

class VirtioChain {
 public:
  VirtioChain() = default;
  VirtioChain(VirtioQueue* queue, uint16_t head);
  VirtioChain(VirtioChain&&) = default;
  VirtioChain& operator=(VirtioChain&&) = default;

  VirtioChain(const VirtioChain&) = delete;
  VirtioChain& operator=(const VirtioChain&) = delete;

  bool IsValid() const;
  bool HasDescriptor() const;
  bool NextDescriptor(VirtioDescriptor* desc);
  uint32_t* Used();
  void Return(uint8_t actions = VirtioQueue::InterruptAction::SET_QUEUE |
                                VirtioQueue::InterruptAction::TRY_INTERRUPT);

 private:
  VirtioQueue* queue_ = nullptr;
  uint32_t used_ = 0;
  uint16_t head_ = 0;
  uint16_t next_ = 0;
  bool has_next_ = false;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEVICE_VIRTIO_QUEUE_H_
