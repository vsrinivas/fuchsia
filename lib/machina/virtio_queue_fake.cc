// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_fake.h"

#include <fbl/alloc_checker.h>
#include <string.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioQueueFake::VirtioQueueFake(VirtioQueue* queue, uint16_t queue_size)
    : queue_(queue), ring_(&queue_->ring_), queue_size_(queue_size) {
  size_t desc_size = sizeof(*VirtioRing::desc) * queue_size;
  size_t avail_size = sizeof(*VirtioRing::avail) +
                      (sizeof(*vring_avail::ring) * queue_size) +
                      sizeof(*VirtioRing::used_event);
  size_t used_size = sizeof(*VirtioRing::used) +
                     (sizeof(*vring_used::ring) * queue_size) +
                     sizeof(*VirtioRing::avail_event);
  zx_status_t status = phys_mem_.Init(desc_size + avail_size + used_size);
  FXL_CHECK(status == ZX_OK) << "Failed to initialize guest physical memory";

  auto desc = reinterpret_cast<zx_gpaddr_t>(phys_mem_.as<void>(0, desc_size));
  auto avail =
      reinterpret_cast<zx_gpaddr_t>(phys_mem_.as<void>(desc_size, avail_size));
  auto used = reinterpret_cast<zx_gpaddr_t>(
      phys_mem_.as<void>(desc_size + avail_size, used_size));
  queue_->Configure(queue_size, desc, avail, used);

  // Disable interrupt generation.
  ring_->used->flags = 1;
  *const_cast<uint16_t*>(ring_->used_event) = 0xffff;
}

zx_status_t VirtioQueueFake::SetNext(uint16_t desc_index, uint16_t next_index) {
  if (desc_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (next_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(queue_->mutex_);
  auto& desc = const_cast<volatile vring_desc&>(ring_->desc[desc_index]);
  desc.flags |= VRING_DESC_F_NEXT;
  desc.next = next_index;
  return ZX_OK;
}

zx_status_t VirtioQueueFake::WriteDescriptor(void* buf, size_t len,
                                             uint16_t flags,
                                             uint16_t* desc_out) {
  uint16_t desc_index = next_free_desc_;
  if (desc_index >= queue_size_) {
    return ZX_ERR_NO_MEMORY;
  }

  next_free_desc_++;

  std::lock_guard<std::mutex> lock(queue_->mutex_);
  auto& desc = const_cast<volatile vring_desc&>(ring_->desc[desc_index]);
  desc.addr = reinterpret_cast<uint64_t>(buf);
  desc.len = static_cast<uint32_t>(len);
  desc.flags = flags;

  if (desc_out != nullptr) {
    *desc_out = desc_index;
  }
  return ZX_OK;
}

void VirtioQueueFake::WriteToAvail(uint16_t desc) {
  std::lock_guard<std::mutex> lock(queue_->mutex_);
  auto avail = const_cast<volatile vring_avail*>(ring_->avail);
  uint16_t& avail_idx = const_cast<uint16_t&>(ring_->avail->idx);
  avail->ring[avail_idx++ % queue_size_] = desc;
}

bool VirtioQueueFake::HasUsed() const {
  std::lock_guard<std::mutex> lock(queue_->mutex_);
  return ring_->used->idx != used_index_;
}

struct vring_used_elem VirtioQueueFake::NextUsed() {
  FXL_DCHECK(HasUsed());
  std::lock_guard<std::mutex> lock(queue_->mutex_);
  return const_cast<struct vring_used_elem&>(
      ring_->used->ring[used_index_++ % queue_size_]);
}

zx_status_t DescBuilder::Build(uint16_t* desc) {
  if (status_ == ZX_OK) {
    queue_->WriteToAvail(head_desc_);
    if (desc != nullptr) {
      *desc = head_desc_;
    }
    head_desc_ = 0;
    prev_desc_ = 0;
    len_ = 0;
    // Signal so that queue event signals will be set.
    status_ = queue_->queue()->Notify();
  }
  return status_;
}

DescBuilder& DescBuilder::Append(void* buf, size_t buf_len, bool write) {
  // If a previous Append operation failed just no-op.
  if (status_ != ZX_OK) {
    return *this;
  }

  uint16_t flags = write ? VRING_DESC_F_WRITE : 0;
  uint16_t desc;
  status_ = queue_->WriteDescriptor(buf, buf_len, flags, &desc);
  if (status_ == ZX_OK) {
    if (len_++ == 0) {
      head_desc_ = desc;
    } else {
      status_ = queue_->SetNext(prev_desc_, desc);
    }

    prev_desc_ = desc;
  }

  return *this;
}

}  // namespace machina
