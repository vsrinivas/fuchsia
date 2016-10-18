// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BUFFER_H
#define PLATFORM_BUFFER_H

#include "magma_util/dlog.h"
#include <memory>

namespace magma {

class OpaquePlatformBuffer {
public:
    virtual ~OpaquePlatformBuffer() {}

    // returns the size of the buffer
    virtual uint64_t size() = 0;

    // returns a unique, immutable id for the underlying memory object
    virtual uint64_t id() = 0;

    // on success, duplicate of the underlying handle which is owned by the caller
    virtual bool duplicate_handle(uint32_t* handle_out) = 0;

    static bool IdFromHandle(uint32_t handle, uint64_t* id_out);
};

class PlatformBuffer : public OpaquePlatformBuffer {
public:
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size);
    static std::unique_ptr<PlatformBuffer> Import(uint32_t handle);

    virtual ~PlatformBuffer() override {}

    virtual bool MapCpu(void** addr_out) = 0;
    virtual bool UnmapCpu() = 0;

    virtual bool PinPages(uint32_t start_page_index, uint32_t page_count) = 0;
    virtual bool UnpinPages(uint32_t start_page_index, uint32_t page_count) = 0;

    virtual bool MapPageCpu(uint32_t page_index, void** addr_out) = 0;
    virtual bool UnmapPageCpu(uint32_t page_index) = 0;

    virtual bool MapPageBus(uint32_t page_index, uint64_t* addr_out) = 0;
    virtual bool UnmapPageBus(uint32_t page_index) = 0;
};

} // namespace magma

#endif // PLATFORM_BUFFER_H
