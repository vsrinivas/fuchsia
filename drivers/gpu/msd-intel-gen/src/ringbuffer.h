// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "magma_util/macros.h"
#include "msd_intel_buffer.h"
#include <memory>

class AddressSpace;

class Ringbuffer {
public:
    Ringbuffer(std::unique_ptr<MsdIntelBuffer> buffer);

    uint64_t size() { return size_; }

    void write_tail(uint32_t dword)
    {
        DASSERT(vaddr_);
        vaddr_[tail_ >> 2] = dword;
        tail_ += 4;
        if (tail_ >= size_) {
            DLOG("ringbuffer tail wrapped");
            tail_ = 0;
        }
        DASSERT(tail_ != head_);
    }

    uint32_t tail() { return tail_; }

    uint32_t head() { return head_; }

    void update_head(uint32_t head)
    {
        DASSERT((head & 0x3) == 0);
        DASSERT(head < size_);
        DLOG("updating head 0x%x", head);
        head_ = head;
    }

    bool HasSpace(uint32_t bytes);

    // Maps to both cpu and gpu.
    bool Map(std::shared_ptr<AddressSpace> address_space);
    bool Unmap();

    bool GetGpuAddress(AddressSpaceId id, gpu_addr_t* addr_out);

private:
    uint32_t* vaddr() { return vaddr_; }

    std::shared_ptr<MsdIntelBuffer> buffer_;
    std::unique_ptr<GpuMapping> gpu_mapping_;
    uint64_t size_;
    uint32_t head_;
    uint32_t tail_;
    uint32_t* vaddr_{}; // mapped virtual address

    friend class TestRingbuffer;
};

#endif // RINGBUFFER_H
