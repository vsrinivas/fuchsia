// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtio_queue_fake.h"

#include <string.h>

#include <fbl/alloc_checker.h>

zx_status_t VirtioQueueFake::Init(uint16_t queue_size) {
    fbl::unique_ptr<uint8_t[]> desc;
    fbl::unique_ptr<uint8_t[]> avail;
    fbl::unique_ptr<uint8_t[]> used;

    {
        fbl::AllocChecker ac;
        size_t desc_size = queue_->size * sizeof(queue_->desc[0]);
        desc.reset(new (&ac) uint8_t[desc_size]);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
        memset(desc.get(), 0, desc_size);
    }

    {
        fbl::AllocChecker ac;
        size_t avail_size = sizeof(*queue_->avail) +
            (queue_->size * sizeof(queue_->avail->ring[0])) +
            sizeof(*queue_->used_event);
        avail.reset(new (&ac) uint8_t[avail_size]);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
        memset(avail.get(), 0, avail_size);
    }

    {
        fbl::AllocChecker ac;
        size_t used_size = sizeof(*queue_->used) + (queue_->size * sizeof(queue_->used->ring[0])) +
            sizeof(*queue_->avail_event);
        used.reset(new (&ac) uint8_t[used_size]);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
        memset(used.get(), 0,used_size);
    }

    queue_size_ = queue_size;
    desc_buf_ = fbl::move(desc);
    avail_ring_buf_ = fbl::move(avail);
    used_ring_buf_ = fbl::move(used);

    queue_->size = queue_size;
    virtio_queue_set_desc_addr(queue_, reinterpret_cast<uint64_t>(desc_buf_.get()));
    virtio_queue_set_avail_addr(queue_, reinterpret_cast<uint64_t>(avail_ring_buf_.get()));
    virtio_queue_set_used_addr(queue_, reinterpret_cast<uint64_t>(used_ring_buf_.get()));
    return ZX_OK;
}

VirtioQueueFake::~VirtioQueueFake() {
    queue_->addr.desc = 0;
    queue_->desc = nullptr;
    queue_->addr.avail = 0;
    queue_->avail = nullptr;
    queue_->addr.used = 0;
    queue_->used = nullptr;
}

zx_status_t VirtioQueueFake::SetNext(uint16_t desc_index, uint16_t next_index) {
    if (desc_index >= queue_size_)
        return ZX_ERR_INVALID_ARGS;
    if (next_index >= queue_size_)
        return ZX_ERR_INVALID_ARGS;

    volatile vring_desc& desc = queue_->desc[desc_index];
    desc.flags |= VRING_DESC_F_NEXT;
    desc.next = next_index;
    return ZX_OK;
}

zx_status_t VirtioQueueFake::WriteDescriptor(void* buf, size_t len, uint16_t flags,
                                             uint16_t* desc_out) {
    uint16_t desc_index = next_free_desc_;
    if (desc_index >= queue_size_)
        return ZX_ERR_NO_MEMORY;

    next_free_desc_++;

    volatile vring_desc& desc = queue_->desc[desc_index];
    desc.addr = reinterpret_cast<uint64_t>(buf);
    desc.len = static_cast<uint32_t>(len);
    desc.flags = flags;

    if (desc_out != nullptr)
        *desc_out = desc_index;
    return ZX_OK;
}

void VirtioQueueFake::WriteToAvail(uint16_t desc) {
    queue_->avail->ring[queue_->avail->idx++ % queue_size_] = desc;
}

zx_status_t DescBuilder::Build() {
    if (status_ == ZX_OK) {
        queue_->WriteToAvail(head_desc_);
        head_desc_ = 0;
        prev_desc_ = 0;
        len_ = 0;
    }
    return status_;
}

DescBuilder& DescBuilder::Append(void* buf, size_t buf_len, bool write) {
    // If a previous Append operation failed just no-op.
    if (status_ != ZX_OK)
        return *this;

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
