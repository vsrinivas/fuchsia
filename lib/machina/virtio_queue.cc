// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue.h"

#include <stdio.h>
#include <string.h>

#include <fbl/unique_ptr.h>
#include <lib/fxl/logging.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/machina/virtio_device.h"

namespace machina {

VirtioQueue::VirtioQueue() {
  FXL_CHECK(zx::event::create(0, &event_) == ZX_OK);
}

uint16_t VirtioQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_.size;
}

uint16_t VirtioQueue::avail_event_num() {
  std::lock_guard<std::mutex> lock(mutex_);
  return avail_event_num_;
}

void VirtioQueue::set_avail_event_num(uint16_t num) {
  std::lock_guard<std::mutex> lock(mutex_);
  avail_event_num_ = num;
}

void VirtioQueue::GetAddrs(zx_gpaddr_t* desc_addr, zx_gpaddr_t* avail_addr,
                           zx_gpaddr_t* used_addr) const {
  std::lock_guard<std::mutex> lock(mutex_);
  *desc_addr = ring_.addr.desc;
  *avail_addr = ring_.addr.avail;
  *used_addr = ring_.addr.used;
}

void VirtioQueue::Configure(uint16_t size, zx_gpaddr_t desc_addr,
                            zx_gpaddr_t avail_addr, zx_gpaddr_t used_addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  const PhysMem& phys_mem = device_->phys_mem();

  // Configure the ring size.
  ring_.size = size;

  // Configure the descriptor table.
  ring_.addr.desc = desc_addr;
  const uintptr_t desc_size = ring_.size * sizeof(ring_.desc[0]);
  ring_.desc = phys_mem.as<vring_desc>(desc_addr, desc_size);

  // Configure the available ring.
  ring_.addr.avail = avail_addr;
  const uintptr_t avail_size =
      sizeof(*ring_.avail) + (ring_.size * sizeof(ring_.avail->ring[0]));
  ring_.avail = phys_mem.as<vring_avail>(avail_addr, avail_size);

  const uintptr_t used_event_addr = avail_addr + avail_size;
  ring_.used_event = phys_mem.as<uint16_t>(used_event_addr);

  // Configure the used ring.
  ring_.addr.used = used_addr;
  const uintptr_t used_size =
      sizeof(*ring_.used) + (ring_.size * sizeof(ring_.used->ring[0]));
  ring_.used = phys_mem.as<vring_used>(used_addr, used_size);

  const uintptr_t avail_event_addr = used_addr + used_size;
  ring_.avail_event = phys_mem.as<uint16_t>(avail_event_addr);
}

zx_status_t VirtioQueue::NextAvailLocked(uint16_t* index) {
  if (!HasAvailLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  *index = ring_.avail->ring[RingIndexLocked(ring_.index++)];

  // If we have event indices enabled, update the avail-event to notify us
  // when we have sufficient descriptors available.
  if (device_->has_enabled_features(1u << VIRTIO_F_RING_EVENT_IDX) &&
      ring_.avail_event) {
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

zx_status_t VirtioQueue::Signal() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (HasAvailLocked()) {
    return event_.signal(0, SIGNAL_QUEUE_AVAIL);
  }
  return ZX_OK;
}

struct poll_task_args_t {
  VirtioQueue* queue;
  VirtioQueue::PollFn handler;
  std::string name;
};

static int virtio_queue_poll_task(void* ctx) {
  zx_status_t result = ZX_OK;
  fbl::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
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

zx_status_t VirtioQueue::Poll(PollFn handler, std::string name) {
  auto args = new poll_task_args_t{this, std::move(handler), std::move(name)};

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
  size_t mem_size = device_->phys_mem().size();

  const uint64_t end = desc.addr + desc.len;
  if (end < desc.addr || end > mem_size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  out->addr = device_->phys_mem().as<void>(desc.addr, desc.len);
  out->len = desc.len;
  out->has_next = desc.flags & VRING_DESC_F_NEXT;
  out->writable = desc.flags & VRING_DESC_F_WRITE;
  out->next = desc.next;
  return ZX_OK;
}

zx_status_t VirtioQueue::Return(uint16_t index, uint32_t len,
                                InterruptAction action) {
  bool needs_interrupt = false;
  bool use_event_index =
      device_->has_enabled_features(1u << VIRTIO_F_RING_EVENT_IDX);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    volatile struct vring_used_elem* used =
        &ring_.used->ring[RingIndexLocked(ring_.used->idx)];

    used->id = index;
    used->len = len;
    ring_.used->idx++;

    // Virtio 1.0 Section 2.4.7.2: Virtqueue Interrupt Suppression
    if (!use_event_index) {
      // If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
      //  - The device MUST ignore the used_event value.
      //  - After the device writes a descriptor index into the used ring:
      //    - If flags is 1, the device SHOULD NOT send an interrupt.
      //    - If flags is 0, the device MUST send an interrupt.
      needs_interrupt = ring_.used->flags == 0;
    } else {
      // Otherwise, if the VIRTIO_F_EVENT_IDX feature bit is negotiated:
      //
      //  - The device MUST ignore the lower bit of flags.
      //  - After the device writes a descriptor index into the used ring:
      //    - If the idx field in the used ring (which determined where that
      //      descriptor index was placed) was equal to used_event, the device
      //      MUST send an interrupt.
      //    - Otherwise the device SHOULD NOT send an interrupt.
      if (ring_.used_event) {
        needs_interrupt = ring_.used->idx == (*ring_.used_event + 1);
      }
    }
  }

  if (needs_interrupt) {
    // Set the queue bit in the device ISR so that the driver knows to check
    // the queues on the next interrupt.
    device_->add_isr_flags(VirtioDevice::VIRTIO_ISR_QUEUE);
    if (action == InterruptAction::SEND_INTERRUPT) {
      return device_->NotifyGuest();
    }
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

    void* addr = device_->phys_mem().as<void>(desc->addr, desc->len);
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
