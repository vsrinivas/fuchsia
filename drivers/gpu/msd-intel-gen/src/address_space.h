// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include "gpu_mapping.h"
#include "msd_intel_buffer.h"
#include "pagetable.h"

// Base class for various address spaces.
class AddressSpace {
public:
    AddressSpace(AddressSpaceType type) : type_(type) {}

    virtual ~AddressSpace() {}

    AddressSpaceType type() { return type_; }

    virtual uint64_t Size() const = 0;

    // Allocates space and returns an address to the start of the allocation.
    virtual bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) = 0;

    // Releases the allocation at the given address.
    virtual bool Free(uint64_t addr) = 0;

    // Clears the page table entries for the allocation at the given address.
    virtual bool Clear(uint64_t addr) = 0;

    // Inserts the pages for the given buffer into page table entries for the allocation at the
    // given address.
    virtual bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset,
                        uint64_t length, CachingType caching_type) = 0;

    static std::unique_ptr<GpuMapping> MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                    std::shared_ptr<MsdIntelBuffer> buffer,
                                                    uint64_t offset, uint64_t length,
                                                    uint32_t alignment);

    static std::unique_ptr<GpuMapping> MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                    std::shared_ptr<MsdIntelBuffer> buffer,
                                                    uint32_t alignment)
    {
        return MapBufferGpu(address_space, buffer, 0, buffer->platform_buffer()->size(), alignment);
    }

    static std::shared_ptr<GpuMapping>
    GetSharedGpuMapping(std::shared_ptr<AddressSpace> address_space,
                        std::shared_ptr<MsdIntelBuffer> buffer, uint64_t offset, uint64_t length,
                        uint32_t alignment);

    static std::shared_ptr<GpuMapping>
    GetSharedGpuMapping(std::shared_ptr<AddressSpace> address_space,
                        std::shared_ptr<MsdIntelBuffer> buffer, uint32_t alignment)
    {
        return GetSharedGpuMapping(address_space, buffer, 0, buffer->platform_buffer()->size(),
                                   alignment);
    }

    uint64_t GetMappedSize(uint64_t buffer_size) { return magma::round_up(buffer_size, PAGE_SIZE); }

private:
    AddressSpaceType type_;
};

#endif // ADDRESS_SPACE_H
