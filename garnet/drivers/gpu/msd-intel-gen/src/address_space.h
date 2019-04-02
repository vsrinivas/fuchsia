// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include "gpu_mapping.h"
#include "magma_util/status.h"
#include "msd_intel_buffer.h"
#include "pagetable.h"
#include "platform_bus_mapper.h"
#include <map>
#include <mutex>
#include <unordered_map>

// Base class for various address spaces.
class AddressSpace {
public:
    class Owner {
    public:
        virtual magma::PlatformBusMapper* GetBusMapper() = 0;
    };

    AddressSpace(Owner* owner, AddressSpaceType type) : owner_(owner), type_(type) {}

    virtual ~AddressSpace() {}

    AddressSpaceType type() { return type_; }

    virtual uint64_t Size() const = 0;

    // Allocates space and returns an address to the start of the allocation.
    // May return false if the AddressSpace doesn't support allocation.
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return AllocLocked(size, align_pow2, addr_out);
    }

    // Releases the allocation at the given address.
    bool Free(uint64_t addr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return FreeLocked(addr);
    }

    // Inserts the pages for the given buffer into page table entries for the allocation at the
    // given address.
    bool Insert(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return InsertLocked(addr, std::move(bus_mapping));
    }

    // Clears the page table entries for the allocation at the given address.
    bool Clear(uint64_t addr, uint64_t page_count)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ClearLocked(addr, page_count);
    }

    // Maps the given |buffer| to a gpu address created from the |address_space| allocator.
    // The address space must support allocation.
    static std::unique_ptr<GpuMapping> MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                    std::shared_ptr<MsdIntelBuffer> buffer,
                                                    uint64_t offset, uint64_t length);

    static std::unique_ptr<GpuMapping> MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                    std::shared_ptr<MsdIntelBuffer> buffer)
    {
        return MapBufferGpu(address_space, buffer, 0, buffer->platform_buffer()->size());
    }

    // Maps the given |buffer| at the given gpu address.
    static magma::Status MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                      std::shared_ptr<MsdIntelBuffer> buffer, gpu_addr_t gpu_addr,
                                      uint64_t page_offset, uint64_t page_count,
                                      std::shared_ptr<GpuMapping>* gpu_mapping_out);

    magma::Status GrowMapping(GpuMapping* mapping, uint64_t page_count);

    std::shared_ptr<GpuMapping> FindGpuMapping(std::shared_ptr<MsdIntelBuffer> buffer,
                                               uint64_t offset, uint64_t length) const;
    std::shared_ptr<GpuMapping> FindGpuMapping(uint64_t gpu_addr) const;

    bool AddMapping(std::shared_ptr<GpuMapping> gpu_mapping);

    bool ReleaseMapping(magma::PlatformBuffer* buffer, gpu_addr_t gpu_addr,
                        std::shared_ptr<GpuMapping>* mapping_out);

    void ReleaseBuffer(magma::PlatformBuffer* buffer,
                       std::vector<std::shared_ptr<GpuMapping>>* released_mappings_out);

    static uint64_t GetMappedSize(uint64_t buffer_size)
    {
        return magma::round_up(buffer_size, PAGE_SIZE);
    }

protected:
    virtual bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) = 0;
    virtual bool FreeLocked(uint64_t addr) = 0;
    virtual bool ClearLocked(uint64_t addr, uint64_t page_count) = 0;
    virtual bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) = 0;

    std::mutex& mutex() { return mutex_; }

private:
    Owner* owner_;
    AddressSpaceType type_;
    using map_container_t = std::map<gpu_addr_t, std::shared_ptr<GpuMapping>>;
    // Container of gpu mappings by address
    map_container_t mappings_;
    // Container of references to entries in mappings_ by buffer;
    // useful for cleaning up mappings when connections go away, and when
    // buffers are released.
    std::unordered_multimap<magma::PlatformBuffer*, map_container_t::iterator> mappings_by_buffer_;
    // Used to keep mutually exclusive access to Alloc, Free, Insert, Clear.
    std::mutex mutex_;
};

#endif // ADDRESS_SPACE_H
