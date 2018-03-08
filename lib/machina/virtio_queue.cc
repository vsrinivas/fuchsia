// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue.h"

#include <stdio.h>
#include <string.h>

#include <fbl/unique_ptr.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/machina/virtio_device.h"
#include "lib/fxl/logging.h"

// Convert guest-physical addresses to usable virtual addresses.
#define guest_paddr_to_host_vaddr(device, guest_paddr) \
  (static_cast<zx_vaddr_t>(((device)->phys_mem().addr()) + (guest_paddr)))

namespace machina {

VirtioQueue::VirtioQueue() {
  ring_.index = 0;
}

// Returns a circular index into a Virtio ring.
constexpr uint32_t ring_index(uint16_t queue_size, uint32_t index) {
  return index % queue_size;
}

static bool ring_has_avail(virtio_queue_t* queue) {
  if (queue->avail == nullptr) {
    return 0;
  }
  return queue->avail->idx != queue->index;
}

static bool validate_queue_range(VirtioDevice* device,
                                 zx_vaddr_t addr,
                                 size_t size) {
  uintptr_t mem_addr = device->phys_mem().addr();
  size_t mem_size = device->phys_mem().size();
  zx_vaddr_t range_end = addr + size;
  zx_vaddr_t mem_end = mem_addr + mem_size;

  return addr >= mem_addr && range_end <= mem_end;
}

template <typename T>
static void queue_set_segment_addr(VirtioQueue* queue,
                                   uint64_t guest_paddr,
                                   size_t size,
                                   T** ptr) {
  VirtioDevice* device = queue->device();
  zx_vaddr_t host_vaddr = guest_paddr_to_host_vaddr(device, guest_paddr);

  *ptr = validate_queue_range(device, host_vaddr, size)
             ? reinterpret_cast<T*>(host_vaddr)
             : nullptr;
}

void VirtioQueue::set_desc_addr(uint64_t desc_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.desc = desc_paddr;
  uintptr_t desc_size = ring_.size * sizeof(ring_.desc[0]);
  queue_set_segment_addr(this, desc_paddr, desc_size, &ring_.desc);
}

uint64_t VirtioQueue::desc_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.desc;
}

void VirtioQueue::set_avail_addr(uint64_t avail_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.avail = avail_paddr;
  uintptr_t avail_size =
      sizeof(*ring_.avail) + (ring_.size * sizeof(ring_.avail->ring[0]));
  queue_set_segment_addr(this, avail_paddr, avail_size, &ring_.avail);

  uintptr_t used_event_paddr = avail_paddr + avail_size;
  uintptr_t used_event_size = sizeof(*ring_.used_event);
  queue_set_segment_addr(this, used_event_paddr, used_event_size,
                         &ring_.used_event);
}

uint64_t VirtioQueue::avail_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.avail;
}

void VirtioQueue::set_used_addr(uint64_t used_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.used = used_paddr;
  uintptr_t used_size =
      sizeof(*ring_.used) + (ring_.size * sizeof(ring_.used->ring[0]));
  queue_set_segment_addr(this, used_paddr, used_size, &ring_.used);

  uintptr_t avail_event_paddr = used_paddr + used_size;
  uintptr_t avail_event_size = sizeof(*ring_.avail_event);
  queue_set_segment_addr(this, avail_event_paddr, avail_event_size,
                         &ring_.avail_event);
}

uint64_t VirtioQueue::used_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.used;
}

void VirtioQueue::Signal() {
  fbl::AutoLock lock(&mutex_);
  if (ring_has_avail(&ring_)) {
    cnd_signal(&avail_ring_cnd_);
  }
}

// This must not return any errors besides |ZX_ERR_SHOULD_WAIT|.
zx_status_t VirtioQueue::NextAvailLocked(uint16_t* index) {
  if (!ring_has_avail(&ring_)) {
    return ZX_ERR_SHOULD_WAIT;
  }

  *index = ring_.avail->ring[ring_index(ring_.size, ring_.index++)];
  return ZX_OK;
}

zx_status_t VirtioQueue::NextAvail(uint16_t* index) {
  fbl::AutoLock lock(&mutex_);
  return NextAvailLocked(index);
}

void VirtioQueue::Wait(uint16_t* index) {
  fbl::AutoLock lock(&mutex_);
  while (NextAvailLocked(index) == ZX_ERR_SHOULD_WAIT) {
    cnd_wait(&avail_ring_cnd_, mutex_.GetInternal());
  }
}

struct poll_task_args_t {
  VirtioQueue* queue;
  virtio_queue_poll_fn_t handler;
  std::string name;
  void* ctx;
};

static int virtio_queue_poll_task(void* ctx) {
  zx_status_t result = ZX_OK;
  fbl::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
  while (true) {
    uint16_t descriptor;
    args->queue->Wait(&descriptor);

    uint32_t used = 0;
    zx_status_t status =
        args->handler(args->queue, descriptor, &used, args->ctx);
    args->queue->Return(descriptor, used);

    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status
                     << " while handling queue buffer for queue " << args->name;
      result = status;
      break;
    }

    result = args->queue->device()->NotifyGuest();
    if (result != ZX_OK) {
      break;
    }
  }

  return result;
}

zx_status_t VirtioQueue::Poll(virtio_queue_poll_fn_t handler,
                              void* ctx,
                              std::string name) {
  auto args = new poll_task_args_t{this, handler, std::move(name), ctx};

  thrd_t thread;
  int ret = thrd_create_with_name(&thread, virtio_queue_poll_task, args,
                                  args->name.c_str());
  if (ret != thrd_success) {
    delete args;
    FXL_LOG(ERROR) << "Failed to create queue thread " << ret;
    return ZX_ERR_INTERNAL;
  }

  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to detach queue thread " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t VirtioQueue::ReadDesc(uint16_t desc_index, virtio_desc_t* out) {
  fbl::AutoLock lock(&mutex_);
  auto& desc = ring_.desc[desc_index];
  size_t mem_size = device_->phys_mem().size();

  const uint64_t end = desc.addr + desc.len;
  if (end < desc.addr || end > mem_size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  out->addr =
      reinterpret_cast<void*>(guest_paddr_to_host_vaddr(device_, desc.addr));
  out->len = desc.len;
  out->has_next = desc.flags & VRING_DESC_F_NEXT;
  out->writable = desc.flags & VRING_DESC_F_WRITE;
  out->next = desc.next;
  return ZX_OK;
}

void VirtioQueue::Return(uint16_t index, uint32_t len) {
  bool needs_interrupt;
  {
    fbl::AutoLock lock(&mutex_);
    volatile struct vring_used_elem* used =
        &ring_.used->ring[ring_index(ring_.size, ring_.used->idx)];

    used->id = index;
    used->len = len;
    ring_.used->idx++;

    // Virtio 1.0 Section 2.4.7.2: Virtqueue Interrupt Suppression
    //  - If flags is 1, the device SHOULD NOT send an interrupt.
    //  - If flags is 0, the device MUST send an interrupt.
    needs_interrupt = ring_.used->flags == 0;
  }

  if (needs_interrupt) {
    // Set the queue bit in the device ISR so that the driver knows to check
    // the queues on the next interrupt.
    device()->add_isr_flags(VirtioDevice::VIRTIO_ISR_QUEUE);
  }
}

zx_status_t VirtioQueue::HandleDescriptor(virtio_queue_fn_t handler,
                                          void* ctx) {
  uint16_t head;
  uint32_t used_len = 0;
  uintptr_t mem_addr = device()->phys_mem().addr();
  size_t mem_size = device()->phys_mem().size();

  // Get the next descriptor from the available ring. If none are available
  // we can just no-op.
  zx_status_t status = NextAvail(&head);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ZX_OK;
  }
  if (status != ZX_OK) {
    return status;
  }

  status = ZX_OK;
  uint16_t desc_index = head;
  volatile const struct vring_desc* desc;
  do {
    if (desc_index >= size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    {
      fbl::AutoLock lock(&mutex_);
      desc = &ring_.desc[desc_index];
    }

    const uint64_t end = desc->addr + desc->len;
    if (end < desc->addr || end > mem_size) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    void* addr = reinterpret_cast<void*>(mem_addr + desc->addr);
    status = handler(addr, desc->len, desc->flags, &used_len, ctx);
    if (status != ZX_OK) {
      return status;
    }

    desc_index = desc->next;
  } while (desc->flags & VRING_DESC_F_NEXT);

  Return(head, used_len);
  return ring_has_avail(&ring_) ? ZX_ERR_NEXT : ZX_OK;
}

}  // namespace machina
