// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_util/refcounted.h"
#include "msd_platform_buffer.h"
#include <limits.h> // PAGE_SIZE
#include <stdint.h>

class MockPlatformBuffer : public msd_platform_buffer, public magma::Refcounted {
public:
    MockPlatformBuffer(uint64_t size) : magma::Refcounted("MockPlatformBuffer")
    {
        magic_ = kMagic;
        handle_ = handle_count_++;

        size_ = magma::round_up(size, PAGE_SIZE);
        num_pages_ = size / PAGE_SIZE;
        virt_addr_ = new uint8_t[size_]();
    }
    virtual ~MockPlatformBuffer() {}

    uint64_t size() { return size_; }

    uint32_t handle() { return handle_; }

    uint8_t* virt_addr() { return virt_addr_; }

    uint64_t num_pages() { return num_pages_; }

    static MockPlatformBuffer* cast(msd_platform_buffer* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MockPlatformBuffer*>(buf);
    }

private:
    static uint32_t handle_count_;

    uint64_t size_;
    uint8_t* virt_addr_;
    uint32_t handle_;
    uint64_t num_pages_;

    static const uint32_t kMagic = 0x6d6b7062; // "mkpb" (Mock Platform Buffer)
};