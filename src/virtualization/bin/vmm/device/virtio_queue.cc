// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

#include <virtio/virtio_ring.h>

#include "src/lib/syslog/cpp/logger.h"

VirtioQueue::VirtioQueue() { FX_CHECK(zx::event::create(0, &event_) == ZX_OK); }

void VirtioQueue::Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail, zx_gpaddr_t used) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Configure the ring size.
  ring_.size = size;

  // Configure the descriptor table.
  const uintptr_t desc_size = ring_.size * sizeof(ring_.desc[0]);
  ring_.desc = phys_mem_->as<vring_desc>(desc, desc_size);

  // Configure the available ring.
  const uintptr_t avail_size = sizeof(*ring_.avail) + (ring_.size * sizeof(ring_.avail->ring[0]));
  ring_.avail = phys_mem_->as<vring_avail>(avail, avail_size);

  const uintptr_t used_event_addr = avail + avail_size;
  ring_.used_event = phys_mem_->as<uint16_t>(used_event_addr);

  // Configure the used ring.
  const uintptr_t used_size = sizeof(*ring_.used) + (ring_.size * sizeof(ring_.used->ring[0]));
  ring_.used = phys_mem_->as<vring_used>(used, used_size);

  const uintptr_t avail_event_addr = used + used_size;
  ring_.avail_event = phys_mem_->as<uint16_t>(avail_event_addr);
}

bool VirtioQueue::NextChain(VirtioChain* chain) {
  uint16_t head;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!HasAvailLocked()) {
      return false;
    }
    head = ring_.avail->ring[RingIndexLocked(ring_.index++)];
    if (head >= ring_.size) {
      return false;
    }
    if (use_event_index_ && ring_.avail_event) {
      *ring_.avail_event = ring_.index;
    }
  }
  *chain = VirtioChain(this, head);
  return true;
}

zx_status_t VirtioQueue::NextAvailLocked(uint16_t* index) {
  if (!HasAvailLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  *index = ring_.avail->ring[RingIndexLocked(ring_.index++)];
  if (*index >= ring_.size) {
    return ZX_ERR_INTERNAL;
  }

  // If we have event indices enabled, update the avail-event to notify us
  // when we have sufficient descriptors available.
  if (use_event_index_ && ring_.avail_event) {
    *ring_.avail_event = ring_.index;
  }

  if (!HasAvailLocked()) {
    return event_.signal(SIGNAL_QUEUE_AVAIL, 0);
  }
  return ZX_OK;
}

bool VirtioQueue::HasAvailLocked() const {
  // Load the avail index with acquire semantics. We know that the guest will have written to
  // idx with at least release semantics after filling in the descriptor information, so by
  // doing an acquire we ensure that the load of any descriptor information is forced to happen
  // after this point and cannot be cached or read earlier.
  return ring_.avail != nullptr &&
         __atomic_load_n(&ring_.avail->idx, __ATOMIC_ACQUIRE) != ring_.index;
}

uint32_t VirtioQueue::RingIndexLocked(uint32_t index) const { return index % ring_.size; }

zx_status_t VirtioQueue::Notify() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (HasAvailLocked()) {
    return event_.signal(0, SIGNAL_QUEUE_AVAIL);
  }
  return ZX_OK;
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
  out->next = desc.next;
  out->has_next = desc.flags & VRING_DESC_F_NEXT;
  out->writable = desc.flags & VRING_DESC_F_WRITE;
  return ZX_OK;
}

zx_status_t VirtioQueue::Return(uint16_t index, uint32_t len, uint8_t actions) {
  bool needs_interrupt = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    volatile struct vring_used_elem* used = &ring_.used->ring[RingIndexLocked(ring_.used->idx)];

    used->id = index;
    used->len = len;
    // Update the used index with a release to ensure that all our previous writes are
    // made visible to the guest before it can observe that the index has changed.
    // We do not need the increment to be atomic, we only require that a memory order
    // be enforced, since there will be no other writers to this location and so we
    // can use the cheaper __atomic_store instead of __atomic_add_fetch
    __atomic_store_n(&ring_.used->idx, ring_.used->idx + 1, __ATOMIC_RELEASE);

    // Virtio 1.0 Section 2.4.7.2: Virtqueue Interrupt Suppression
    if (!use_event_index_) {
      // If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
      //  - The device MUST ignore the used_event value.
      //  - After the device writes a descriptor index into the used ring:
      //    - If flags is 1, the device SHOULD NOT send an interrupt.
      //    - If flags is 0, the device MUST send an interrupt.
      needs_interrupt = ring_.avail->flags == 0;
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

VirtioChain::VirtioChain(VirtioQueue* queue, uint16_t head)
    : queue_(queue), head_(head), next_(head), has_next_(true) {}

VirtioChain::~VirtioChain() { FX_CHECK(!IsValid()) << "Descriptor chain leak."; }

VirtioChain::VirtioChain(VirtioChain&& o)
    : queue_(o.queue_), used_(o.used_), head_(o.head_), next_(o.next_), has_next_(o.has_next_) {
  o.Reset();
}

VirtioChain& VirtioChain::operator=(VirtioChain&& o) {
  FX_CHECK(!IsValid()) << "Moving into valid chain. This would leak a descriptor chain.";

  queue_ = o.queue_;
  used_ = o.used_;
  head_ = o.head_;
  next_ = o.next_;
  has_next_ = o.has_next_;

  o.Reset();
  return *this;
}

bool VirtioChain::IsValid() const { return queue_ != nullptr; }

bool VirtioChain::HasDescriptor() const { return has_next_; }

bool VirtioChain::NextDescriptor(VirtioDescriptor* desc) {
  if (!HasDescriptor()) {
    return false;
  }
  zx_status_t status = queue_->ReadDesc(next_, desc);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to read queue " << status;
    return false;
  }
  next_ = desc->next;
  has_next_ = desc->has_next;
  return true;
}

uint32_t* VirtioChain::Used() { return &used_; }

void VirtioChain::Return(uint8_t actions) {
  FX_CHECK(IsValid()) << "Attempting to return an invalid descriptor";
  zx_status_t status = queue_->Return(head_, used_, actions);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to return descriptor chain to queue " << status;
  }
  Reset();
}

void VirtioChain::Reset() {
  queue_ = nullptr;
  used_ = 0;
  head_ = 0;
  next_ = 0;
  has_next_ = 0;
}
