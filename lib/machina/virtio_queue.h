// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_
#define GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_

#include <fbl/mutex.h>
#include <virtio/virtio.h>
#include <zircon/types.h>

struct vring_desc;
struct vring_avail;
struct vring_used;

namespace machina {

class VirtioDevice;

// Stores the Virtio queue based on the ring provided by the guest.
//
// NOTE(abdulla): This structure points to guest-controlled memory.
typedef struct virtio_queue {
  mtx_t mutex;
  // Allow threads to block on buffers in the avail ring.
  cnd_t avail_ring_cnd;

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

  // Pointer to the owning device.
  VirtioDevice* virtio_device;

  volatile struct vring_desc* desc;  // guest-controlled

  volatile struct vring_avail* avail;  // guest-controlled
  volatile uint16_t* used_event;       // guest-controlled

  volatile struct vring_used* used;  // guest-controlled
  volatile uint16_t* avail_event;    // guest-controlled
} virtio_queue_t;

// Callback function for virtio_queue_handler.
//
// For chained buffers uing VRING_DESC_F_NEXT, this function will be called once
// for each buffer in the chain.
//
// addr     - Pointer to the descriptor buffer.
// len      - Length of the descriptor buffer.
// flags    - Flags from the vring descriptor.
// used     - To be incremented by the number of bytes used from addr.
// ctx      - The same pointer passed to virtio_queue_handler.
typedef zx_status_t (*virtio_queue_fn_t)(void* addr,
                                         uint32_t len,
                                         uint16_t flags,
                                         uint32_t* used,
                                         void* ctx);

// Handles the next available descriptor in a Virtio queue, calling handler to
// process individual payload buffers.
//
// On success the function either returns ZX_OK if there are no more descriptors
// available, or ZX_ERR_NEXT if there are more available descriptors to process.
zx_status_t virtio_queue_handler(virtio_queue_t* queue,
                                 virtio_queue_fn_t handler,
                                 void* ctx);

// Get the index of the next descriptor in the available ring.
//
// If a buffer is a available, the descriptor index is written to |index|, the
// queue index pointer is incremented, and ZX_OK is returned.
//
// If no buffers are available ZX_ERR_NOT_FOUND is returned.
zx_status_t virtio_queue_next_avail(virtio_queue_t* queue, uint16_t* index);

// Blocking variant of virtio_queue_next_avail.
void virtio_queue_wait(virtio_queue_t* queue, uint16_t* index);

// Notify waiting threads blocked on |virtio_queue_wait| that the avail ring
// has descriptors available.
void virtio_queue_signal(virtio_queue_t* queue);

// Sets the address of the descriptor table for this queue.
void virtio_queue_set_desc_addr(virtio_queue_t* queue, uint64_t desc_addr);

// Sets the address of the available ring for this queue.
void virtio_queue_set_avail_addr(virtio_queue_t* queue, uint64_t avail_addr);

// Sets the address of the used ring for this queue.
void virtio_queue_set_used_addr(virtio_queue_t* queue, uint64_t used_addr);

// Callback for virtio_queue_poll.
//
// queue    - The queue being polled.
// head     - Descriptor index of the buffer chain to process.
// used     - To be incremented by the number of bytes used from addr.
// ctx      - The same pointer passed to virtio_queue_poll.
//
// The queue will continue to be polled as long as this method returns ZX_OK.
// The error ZX_ERR_STOP will be treated as a special value to indicate queue
// polling should stop gracefully and terminate the thread.
//
// Any other error values will be treated as unexpected errors that will cause
// the polling thread to be terminated with a non-zero exit value.
typedef zx_status_t (*virtio_queue_poll_fn_t)(virtio_queue_t* queue,
                                              uint16_t head,
                                              uint32_t* used,
                                              void* ctx);

// Spawn a thread to wait for descriptors to be available and invoke the
// provided handler on each available buffer asyncronously.
zx_status_t virtio_queue_poll(virtio_queue_t* queue,
                              virtio_queue_poll_fn_t handler,
                              void* ctx,
                              const char* thread_name);

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

// Reads a single descriptor from the queue.
//
// This method should only be called using descriptor indicies acquired with
// virtio_queue_next_avail (including any chained decriptors) and before
// they've been released with virtio_queue_return.
zx_status_t virtio_queue_read_desc(virtio_queue_t* queue,
                                   uint16_t index,
                                   virtio_desc_t* desc);

// Return a descriptor to the used ring.
//
// |index| must be a value received from a call to virtio_queue_next_avail. Any
// buffers accessed via |index| or any chained descriptors must not be used
// after calling virtio_queue_return.
void virtio_queue_return(virtio_queue_t* queue, uint16_t index, uint32_t len);


}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_QUEUE_H_
