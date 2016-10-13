// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BUFFER_H
#define PLATFORM_BUFFER_H

#include "magma_util/dlog.h"
#include "msd_platform_buffer.h"
#include <memory>

namespace magma {

class PlatformBuffer {
public:
    // Returned token is owned by the returned unique_ptr and will become invalid when the
    // unique_ptr goes out of scope
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size, msd_platform_buffer** token_out);
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size);
    static std::unique_ptr<PlatformBuffer> Create(msd_platform_buffer* token);

    ~PlatformBuffer();

    uint64_t size() { return size_; }
    uint32_t handle() { return handle_; }

    // The following are mostly wrappers around the c abi; see msd_platform_buffer.h
    bool MapCpu(void** addr_out);
    bool UnmapCpu();

    bool PinPages(uint32_t start_page_index, uint32_t page_count);
    bool UnpinPages(uint32_t start_page_index, uint32_t page_count);

    bool MapPageCpu(uint32_t page_index, void** addr_out);
    bool UnmapPageCpu(uint32_t page_index);

    bool MapPageBus(uint32_t page_index, uint64_t* addr_out);
    bool UnmapPageBus(uint32_t page_index);

    uint32_t GetRefCount();

    PlatformBuffer(const PlatformBuffer&) = delete;
    void operator=(const PlatformBuffer&) = delete;

private:
    PlatformBuffer(msd_platform_buffer* token, uint64_t size, uint32_t handle);

    msd_platform_buffer* token_;
    uint64_t size_;
    uint32_t handle_;
};

} // namespace magma

#endif // PLATFORM_BUFFER_H
