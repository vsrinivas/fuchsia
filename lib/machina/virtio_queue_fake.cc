// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_queue_fake.h"

#include <fbl/alloc_checker.h>
#include <string.h>

#include "lib/fxl/logging.h"

namespace machina {

VirtioQueueFake::VirtioQueueFake(VirtioQueue* queue) : queue_(queue) {
  // Get ring from queue for testing, unsafe under other circumstances.
  queue_->UpdateRing<void>([this](VirtioRing* r) { ring_ = r; });
}

VirtioQueueFake::~VirtioQueueFake() { queue_->Configure(0, 0, 0, 0); }

zx_status_t VirtioQueueFake::Init(uint16_t queue_size) {
  fbl::unique_ptr<uint8_t[]> desc;
  fbl::unique_ptr<uint8_t[]> avail;
  fbl::unique_ptr<uint8_t[]> used;

  {
    fbl::AllocChecker ac;
    size_t desc_size = queue_->size() * sizeof(ring_->desc[0]);
    desc.reset(new (&ac) uint8_t[desc_size]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    memset(desc.get(), 0, desc_size);
  }

  {
    fbl::AllocChecker ac;
    size_t avail_size = sizeof(*ring_->avail) +
                        (queue_->size() * sizeof(ring_->avail->ring[0])) +
                        sizeof(*ring_->used_event);
    avail.reset(new (&ac) uint8_t[avail_size]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    memset(avail.get(), 0, avail_size);
  }

  {
    fbl::AllocChecker ac;
    size_t used_size = sizeof(*ring_->used) +
                       (ring_->size * sizeof(ring_->used->ring[0])) +
                       sizeof(*ring_->avail_event);
    used.reset(new (&ac) uint8_t[used_size]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    memset(used.get(), 0, used_size);
  }

  queue_size_ = queue_size;
  desc_buf_ = std::move(desc);
  avail_ring_buf_ = std::move(avail);
  used_ring_buf_ = std::move(used);

  queue_->Configure(queue_size, reinterpret_cast<zx_gpaddr_t>(desc_buf_.get()),
                    reinterpret_cast<zx_gpaddr_t>(avail_ring_buf_.get()),
                    reinterpret_cast<zx_gpaddr_t>(used_ring_buf_.get()));

  // Disable interrupt generation.
  queue_->UpdateRing<void>([](VirtioRing* ring) {
    ring->used->flags = 1;
    *const_cast<uint16_t*>(ring->used_event) = 0xffff;
  });
  return ZX_OK;
}

zx_status_t VirtioQueueFake::SetNext(uint16_t desc_index, uint16_t next_index) {
  if (desc_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (next_index >= queue_size_) {
    return ZX_ERR_INVALID_ARGS;
  }

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
  auto avail = const_cast<volatile vring_avail*>(ring_->avail);
  uint16_t& avail_idx = const_cast<uint16_t&>(ring_->avail->idx);
  avail->ring[avail_idx++ % queue_size_] = desc;
}

bool VirtioQueueFake::HasUsed() const {
  return ring_->used->idx != used_index_;
}

struct vring_used_elem VirtioQueueFake::NextUsed() {
  FXL_DCHECK(HasUsed());
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
    status_ = queue_->queue()->Signal();
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
