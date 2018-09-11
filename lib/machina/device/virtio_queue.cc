// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/device/virtio_queue.h"

#include <threads.h>

#include <lib/fxl/logging.h>
#include <virtio/virtio_ring.h>

namespace machina {

VirtioQueue::VirtioQueue() {
  FXL_CHECK(zx::event::create(0, &event_) == ZX_OK);
}

void VirtioQueue::Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                            zx_gpaddr_t used) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Configure the ring size.
  ring_.size = size;

  // Configure the descriptor table.
  const uintptr_t desc_size = ring_.size * sizeof(ring_.desc[0]);
  ring_.desc = phys_mem_->as<vring_desc>(desc, desc_size);

  // Configure the available ring.
  const uintptr_t avail_size =
      sizeof(*ring_.avail) + (ring_.size * sizeof(ring_.avail->ring[0]));
  ring_.avail = phys_mem_->as<vring_avail>(avail, avail_size);

  const uintptr_t used_event_addr = avail + avail_size;
  ring_.used_event = phys_mem_->as<uint16_t>(used_event_addr);

  // Configure the used ring.
  const uintptr_t used_size =
      sizeof(*ring_.used) + (ring_.size * sizeof(ring_.used->ring[0]));
  ring_.used = phys_mem_->as<vring_used>(used, used_size);

  const uintptr_t avail_event_addr = used + used_size;
  ring_.avail_event = phys_mem_->as<uint16_t>(avail_event_addr);
}

zx_status_t VirtioQueue::NextAvailLocked(uint16_t* index) {
  if (!HasAvailLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  *index = ring_.avail->ring[RingIndexLocked(ring_.index++)];

  // If we have event indices enabled, update the avail-event to notify us
  // when we have sufficient descriptors available.
  if (use_event_index_ && ring_.avail_event) {
    *ring_.avail_event = ring_.index + avail_event_num_ - 1;
  }

  if (!HasAvailLocked()) {
    return event_.signal(SIGNAL_QUEUE_AVAIL, 0);
  }
  return ZX_OK;
}

zx_status_t VirtioQueue::NextAvail(uint16_t* index) {
  std::lock_guard<std::mutex> lock(mutex_);
  return NextAvailLocked(index);
}

bool VirtioQueue::HasAvailLocked() const {
  if (ring_.avail == nullptr) {
    return false;
  }
  return ring_.avail->idx != ring_.index;
}

uint32_t VirtioQueue::RingIndexLocked(uint32_t index) const {
  return index % ring_.size;
}

void VirtioQueue::Wait(uint16_t* index) {
  zx_status_t status;
  while ((status = NextAvail(index)) == ZX_ERR_SHOULD_WAIT) {
    event_.wait_one(SIGNAL_QUEUE_AVAIL, zx::time::infinite(), nullptr);
  }
  FXL_CHECK(status == ZX_OK);
}

zx_status_t VirtioQueue::Notify() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (HasAvailLocked()) {
    return event_.signal(0, SIGNAL_QUEUE_AVAIL);
  }
  return ZX_OK;
}

struct poll_task_args_t {
  VirtioQueue* queue;
  std::string name;
  VirtioQueue::PollFn handler;
};

static int virtio_queue_poll_task(void* ctx) {
  zx_status_t result = ZX_OK;
  std::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
  while (true) {
    uint16_t descriptor;
    args->queue->Wait(&descriptor);

    uint32_t used = 0;
    zx_status_t status = args->handler(args->queue, descriptor, &used);
    result = args->queue->Return(descriptor, used);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to return descriptor to queue " << result;
      break;
    }

    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status
                     << " while handling queue buffer for queue " << args->name;
      result = status;
      break;
    }
  }

  return result;
}

zx_status_t VirtioQueue::Poll(std::string name, PollFn handler) {
  auto args = new poll_task_args_t{this, std::move(name), std::move(handler)};

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

zx_status_t VirtioQueue::PollAsync(async_dispatcher_t* dispatcher,
                                   async::Wait* wait, PollFn handler) {
  wait->set_object(event_.get());
  wait->set_trigger(SIGNAL_QUEUE_AVAIL);
  wait->set_handler([this, handler = std::move(handler)](
                        async_dispatcher_t* dispatcher, async::Wait* wait,
                        zx_status_t status, const zx_packet_signal_t* signal) {
    InvokeAsyncHandler(dispatcher, wait, status, handler);
  });
  return wait->Begin(dispatcher);
}

void VirtioQueue::InvokeAsyncHandler(async_dispatcher_t* dispatcher,
                                     async::Wait* wait, zx_status_t status,
                                     const PollFn& handler) {
  if (status != ZX_OK) {
    return;
  }

  uint16_t head;
  uint32_t used = 0;
  status = NextAvail(&head);
  if (status == ZX_OK) {
    status = handler(this, head, &used);
    // Try to return the buffer to the queue, even if the handler has failed
    // so we don't leak the descriptor.
    zx_status_t return_status = Return(head, used);
    if (status == ZX_OK) {
      status = return_status;
    }
  }
  if (status == ZX_OK || status == ZX_ERR_SHOULD_WAIT) {
    wait->Begin(dispatcher);  // ignore errors
  }
}

zx_status_t VirtioQueue::ReadDesc(uint16_t desc_index, VirtioDescriptor* out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& desc = ring_.desc[desc_index];

  const uint64_t end = desc.addr + desc.len;
  if (end < desc.addr || end > phys_mem_->size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  out->addr = phys_mem_->as<void>(desc.addr, desc.len);
  out->len = desc.len;
  out->has_next = desc.flags & VRING_DESC_F_NEXT;
  out->writable = desc.flags & VRING_DESC_F_WRITE;
  out->next = desc.next;
  return ZX_OK;
}

zx_status_t VirtioQueue::Return(uint16_t index, uint32_t len, uint8_t actions) {
  bool needs_interrupt = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    volatile struct vring_used_elem* used =
        &ring_.used->ring[RingIndexLocked(ring_.used->idx)];

    used->id = index;
    used->len = len;
    ring_.used->idx++;

    // Virtio 1.0 Section 2.4.7.2: Virtqueue Interrupt Suppression
    if (!use_event_index_) {
      // If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
      //  - The device MUST ignore the used_event value.
      //  - After the device writes a descriptor index into the used ring:
      //    - If flags is 1, the device SHOULD NOT send an interrupt.
      //    - If flags is 0, the device MUST send an interrupt.
      needs_interrupt = ring_.used->flags == 0;
    } else if (ring_.used_event) {
      // Otherwise, if the VIRTIO_F_EVENT_IDX feature bit is negotiated:
      //
      //  - The device MUST ignore the lower bit of flags.
      //  - After the device writes a descriptor index into the used ring:
      //    - If the idx field in the used ring (which determined where that
      //      descriptor index was placed) was equal to used_event, the device
      //      MUST send an interrupt.
      //    - Otherwise the device SHOULD NOT send an interrupt.
      needs_interrupt = ring_.used->idx == (*ring_.used_event + 1);
    }
  }

  if (needs_interrupt) {
    return interrupt_(actions);
  }
  return ZX_OK;
}

zx_status_t VirtioQueue::HandleDescriptor(DescriptorFn handler, void* ctx) {
  uint16_t head;
  uint32_t used_len = 0;

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
      std::lock_guard<std::mutex> lock(mutex_);
      desc = &ring_.desc[desc_index];
    }

    void* addr = phys_mem_->as<void>(desc->addr, desc->len);
    status = handler(addr, desc->len, desc->flags, &used_len, ctx);
    if (status != ZX_OK) {
      return status;
    }

    desc_index = desc->next;
  } while (desc->flags & VRING_DESC_F_NEXT);

  status = Return(head, used_len);
  if (status != ZX_OK) {
    return status;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return HasAvailLocked() ? ZX_ERR_NEXT : ZX_OK;
}

}  // namespace machina
