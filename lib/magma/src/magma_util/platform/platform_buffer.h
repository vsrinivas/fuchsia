// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BUFFER_H
#define PLATFORM_BUFFER_H

#include "magma_util/dlog.h"
#include <memory>

namespace magma {

// In general only the const functions in this class must be implemented in a threadsafe way
class PlatformBuffer {
public:
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size, const char* name);
    // Import takes ownership of the handle.
    static std::unique_ptr<PlatformBuffer> Import(uint32_t handle);
    // ImportFromFd does not close the given file descriptor.
    static std::unique_ptr<PlatformBuffer> ImportFromFd(int fd);

    virtual ~PlatformBuffer() {}

    // returns the size of the buffer
    virtual uint64_t size() const = 0;

    // returns a unique, immutable id for the underlying memory object
    virtual uint64_t id() const = 0;

    // on success, duplicate of the underlying handle which is owned by the caller
    virtual bool duplicate_handle(uint32_t* handle_out) const = 0;

    // creates a new fd which can be used to import this buffer.
    virtual bool GetFd(int* fd_out) const = 0;

    // ensures the specified pages are backed by real memory
    // note: the implementation of this function is required to be threadsafe
    virtual bool CommitPages(uint32_t start_page_index, uint32_t page_count) const = 0;

    // If |alignment| isn't 0, it must be a power of 2 and page-aligned. It's
    // invalid to map the same buffer twice with different alignments.
    virtual bool MapCpu(void** addr_out, uintptr_t alignment = 0) = 0;
    virtual bool UnmapCpu() = 0;

    virtual bool PinPages(uint32_t start_page_index, uint32_t page_count) = 0;
    virtual bool UnpinPages(uint32_t start_page_index, uint32_t page_count) = 0;

    virtual bool MapPageRangeBus(uint32_t start_page_index, uint32_t page_count,
                                 uint64_t addr_out[]) = 0;
    virtual bool UnmapPageRangeBus(uint32_t start_page_index, uint32_t page_count) = 0;

    virtual bool CleanCache(uint64_t offset, uint64_t size, bool invalidate) = 0;

    static bool IdFromHandle(uint32_t handle, uint64_t* id_out);
};

} // namespace magma

#endif // PLATFORM_BUFFER_H
