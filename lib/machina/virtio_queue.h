// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_
#define GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_

#include <string>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <virtio/virtio.h>
#include <zircon/types.h>

struct vring_desc;
struct vring_avail;
struct vring_used;

namespace machina {

class VirtioDevice;
class VirtioQueue;

// Stores the Virtio queue based on the ring provided by the guest.
//
// NOTE(abdulla): This structure points to guest-controlled memory.
typedef struct virtio_queue {
  // Queue addresses as defined in Virtio 1.0 Section 4.1.4.3.
  union {
    struct {
      uint64_t desc;
      uint64_t avail;
      uint64_t used;
    };

    // Software will access these using 32 bit operations. Provide a
    // convenience interface for these use cases.
    uint32_t words[6];
  } addr;

  // Number of entries in the descriptor table.
  uint16_t size;
  uint16_t index;

  const volatile struct vring_desc* desc;  // guest-controlled

  const volatile struct vring_avail* avail;  // guest-controlled
  const volatile uint16_t* used_event;       // guest-controlled

  volatile struct vring_used* used;  // guest-controlled
  volatile uint16_t* avail_event;    // guest-controlled
} virtio_queue_t;

// Callback function for virtio_queue_handler.
//
// For chained buffers using VRING_DESC_F_NEXT, this function will be called
// once for each buffer in the chain.
//
// addr     - Pointer to the descriptor buffer.
// len      - Length of the descriptor buffer.
// flags    - Flags from the vring descriptor.
// used     - To be incremented by the number of bytes used from addr.
// ctx      - The same pointer passed to virtio_queue_handler.
typedef zx_status_t (*virtio_queue_fn_t)(void* addr, uint32_t len,
                                         uint16_t flags, uint32_t* used,
                                         void* ctx);

// A higher-level API for vring_desc.
typedef struct virtio_desc {
  // Pointer to the buffer in our address space.
  void* addr;
  // Number of bytes at addr.
  uint32_t len;
  // Is there another buffer after this one?
  bool has_next;
  // Only valid if has_next is true.
  uint16_t next;
  // If true, this buffer must only be written to (no reads). Otherwise this
  // buffer must only be read from (no writes).
  bool writable;
} virtio_desc_t;

class VirtioQueue {
 public:
  // The signal asserted when there are available descriptors in the queue.
  static constexpr zx_signals_t SIGNAL_QUEUE_AVAIL = ZX_USER_SIGNAL_0;

  VirtioQueue();

  // TODO(tjdetwiler): Temporary escape hatches to allow accessing the
  // underlying ring structure.
  const virtio_queue_t* ring() { return &ring_; }

  template <typename T>
  using RingUpdateFunc = fit::function<T(virtio_queue_t*)>;
  template <typename T>
  T UpdateRing(RingUpdateFunc<T> func) {
    fbl::AutoLock lock(&mutex_);
    return func(&ring_);
  }

  // Gets or sets the associated device with this queue.
  VirtioDevice* device() const { return device_; }
  void set_device(VirtioDevice* device) { device_ = device; }

  // Gets of sets the number of descriptors in the queue.
  uint16_t size() const;
  void set_size(uint16_t size);
  void set_size_unsafe(uint16_t size) __TA_NO_THREAD_SAFETY_ANALYSIS {
    ring_.size = size;
  };

  // If the device negotiates |VIRTIO_F_EVENT_IDX|, this is the number of
  // descriptors to allow the driver to queue into the avail ring before
  // signaling the device that the queue has descriptors.
  //
  // The default value is 1 so that every update to the avail ring causes a
  // notification that descriptors are available.
  //
  // If the device does not negotiate |VIRTIO_F_EVENT_IDX|, this attribute has
  // no effect.
  uint16_t avail_event_num();
  void set_avail_event_num(uint16_t num);

  // Gets or sets the address of the descriptor table for this queue.
  // The address should be in guest physical address space.
  void set_desc_addr(uint64_t desc_addr);
  uint64_t desc_addr() const;

  // Gets or sets the address of the available ring for this queue.
  // The address should be in guest physical address space.
  void set_avail_addr(uint64_t avail_addr);
  uint64_t avail_addr() const;

  // Gets or sets the address of the used ring for this queue.
  // The address should be in guest physical address space.
  void set_used_addr(uint64_t used_addr);
  uint64_t used_addr() const;

  // Returns a handle that can waited on for available descriptors in the.
  // While buffers are available in the queue |ZX_USER_SIGNAL_0| will be
  // asserted.
  zx_handle_t event() const { return event_.get(); }

  // Get the index of the next descriptor in the available ring.
  //
  // If a buffer is a available, the descriptor index is written to |index|, the
  // queue index pointer is incremented, and ZX_OK is returned.
  //
  // If no buffers are available ZX_ERR_SHOULD_WAIT is returned.
  zx_status_t NextAvail(uint16_t* index);

  // Blocking variant of virtio_queue_next_avail.
  //
  // TODO(PD-107): Allow this method to fail.
  void Wait(uint16_t* index);

  // Notify waiting threads blocked on |virtio_queue_wait| that the avail ring
  // has descriptors available.
  zx_status_t Signal();

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
  enum class InterruptAction { SET_FLAGS, SEND_INTERRUPT };
  zx_status_t Return(uint16_t index, uint32_t len,
                     InterruptAction action = InterruptAction::SEND_INTERRUPT);

  // Reads a single descriptor from the queue.
  //
  // This method should only be called using descriptor indices acquired with
  // virtio_queue_next_avail (including any chained descriptors) and before
  // they've been released with virtio_queue_return.
  zx_status_t ReadDesc(uint16_t index, virtio_desc_t* desc);

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
  using QueuePollFn = fit::function<zx_status_t(VirtioQueue* queue,
                                                uint16_t head, uint32_t* used)>;

  // Spawn a thread to wait for descriptors to be available and invoke the
  // provided handler on each available buffer asynchronously.
  //
  // Returns |ZX_ERR_INVALID_ARGS| if |thread_name| is null.
  zx_status_t Poll(QueuePollFn handler, std::string thread_name);

  // Monitors the queue signal for available descriptors and run the callback
  // when one is available.
  zx_status_t PollAsync(async_dispatcher_t* dispatcher, async::Wait* wait, QueuePollFn handler);

  // Handles the next available descriptor in a Virtio queue, calling handler to
  // process individual payload buffers.
  //
  // On success the function either returns ZX_OK if there are no more
  // descriptors available, or ZX_ERR_NEXT if there are more available
  // descriptors to process.
  zx_status_t HandleDescriptor(virtio_queue_fn_t handler, void* ctx);

 private:
  zx_status_t NextAvailLocked(uint16_t* index) __TA_REQUIRES(mutex_);
  bool HasAvailLocked() const __TA_REQUIRES(mutex_);

  // Returns a circular index into a Virtio ring.
  uint32_t RingIndexLocked(uint32_t index) const __TA_REQUIRES(mutex_);

  void InvokeAsyncHandler(async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                          const QueuePollFn& handler);

  mutable fbl::Mutex mutex_;
  VirtioDevice* device_;
  virtio_queue_t ring_ __TA_GUARDED(mutex_) = {};
  zx::event event_;
  uint16_t avail_event_num_ __TA_GUARDED(mutex_) = 1;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_
