// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space.h"
#include "gtt.h"

std::unique_ptr<GpuMapping> AddressSpace::MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                       std::shared_ptr<MsdIntelBuffer> buffer,
                                                       uint64_t offset, uint64_t length)
{
    DASSERT(address_space);
    DASSERT(buffer);

    length = address_space->GetMappedSize(length);

    if (!magma::is_page_aligned(offset))
        return DRETP(nullptr, "offset (0x%lx) not page aligned", offset);

    if (offset + length > buffer->platform_buffer()->size())
        return DRETP(nullptr, "offset (0x%lx) + length (0x%lx) > buffer size (0x%lx)", offset,
                     length, buffer->platform_buffer()->size());

    if (length > address_space->Size())
        return DRETP(nullptr, "length (0x%lx) > address space size (0x%lx)", length,
                     address_space->Size());

    uint64_t align_pow2;
    if (!magma::get_pow2(PAGE_SIZE, &align_pow2))
        return DRETP(nullptr, "alignment is not power of 2");

    // Casting to uint8_t below
    DASSERT((align_pow2 & ~0xFF) == 0);
    DASSERT(magma::is_page_aligned(length));

    gpu_addr_t gpu_addr;
    if (!address_space->Alloc(length, static_cast<uint8_t>(align_pow2), &gpu_addr))
        return DRETP(nullptr, "failed to allocate gpu address");

    DLOG("MapBufferGpu offset 0x%lx length 0x%lx allocated gpu_addr 0x%lx", offset, length,
         gpu_addr);

    uint64_t page_offset = offset / PAGE_SIZE;
    uint32_t page_count = length / PAGE_SIZE;

    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping;

    if (address_space->type() == ADDRESS_SPACE_PPGTT) {
        bus_mapping = address_space->owner_->GetBusMapper()->MapPageRangeBus(
            buffer->platform_buffer(), page_offset, page_count);
        if (!bus_mapping)
            return DRETP(nullptr, "failed to bus map the page range");

        if (!address_space->Insert(gpu_addr, bus_mapping.get()))
            return DRETP(nullptr, "failed to insert into address_space");

    } else {
        if (!static_cast<Gtt*>(address_space.get())
                 ->GlobalGttInsert(gpu_addr, buffer->platform_buffer(), page_offset, page_count))
            return DRETP(nullptr, "failed to insert into address_space");
    }

    return std::unique_ptr<GpuMapping>(
        new GpuMapping(address_space, buffer, offset, length, gpu_addr, std::move(bus_mapping)));
}

magma::Status AddressSpace::MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                         std::shared_ptr<MsdIntelBuffer> buffer,
                                         gpu_addr_t gpu_addr, uint64_t page_offset,
                                         uint64_t page_count,
                                         std::shared_ptr<GpuMapping>* gpu_mapping_out)
{
    DASSERT(address_space);
    DASSERT(address_space->type() == ADDRESS_SPACE_PPGTT);
    DASSERT(buffer);

    if (!magma::is_page_aligned(gpu_addr))
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "gpu_addr 0x%lx not page aligned", gpu_addr);

    magma::PlatformBuffer* platform_buffer = buffer->platform_buffer();

    if ((page_offset + page_count) * PAGE_SIZE > platform_buffer->size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "page_offset (%lu) + page_count (%lu) > buffer size (0x%lx)", page_offset,
                        page_count, platform_buffer->size());

    if (page_count * PAGE_SIZE > address_space->Size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "page_count (%lu) > address space size (0x%lx)",
                        page_count, address_space->Size());

    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping =
        address_space->owner_->GetBusMapper()->MapPageRangeBus(platform_buffer, page_offset,
                                                               page_count);
    if (!bus_mapping)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to map page range to bus");

    if (!address_space->Insert(gpu_addr, bus_mapping.get()))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to insert into address_space");

    *gpu_mapping_out =
        std::make_unique<GpuMapping>(address_space, buffer, page_offset * PAGE_SIZE,
                                     page_count * PAGE_SIZE, gpu_addr, std::move(bus_mapping));
    return MAGMA_STATUS_OK;
}

std::shared_ptr<GpuMapping> AddressSpace::FindGpuMapping(std::shared_ptr<MsdIntelBuffer> buffer,
                                                         uint64_t offset, uint64_t length)
{
    DASSERT(buffer);

    auto range = mappings_by_buffer_.equal_range(buffer->platform_buffer());
    for (auto iter = range.first; iter != range.second; iter++) {
        auto& mapping = iter->second->second;
        if (mapping->offset() == offset && mapping->length() == GetMappedSize(length))
            return mapping;
    }

    return nullptr;
}

bool AddressSpace::AddMapping(std::shared_ptr<GpuMapping> gpu_mapping)
{
    auto iter = mappings_.upper_bound(gpu_mapping->gpu_addr());
    if (iter != mappings_.end() &&
        (gpu_mapping->gpu_addr() + gpu_mapping->length() > iter->second->gpu_addr()))
        return DRETF(false, "Mapping overlaps existing mapping");
    // Find the mapping with the highest VA that's <= this.
    if (iter != mappings_.begin()) {
        --iter;
        // Check if the previous mapping overlaps this.
        if (iter->second->gpu_addr() + iter->second->length() > gpu_mapping->gpu_addr())
            return DRETF(false, "Mapping overlaps existing mapping");
    }

    std::pair<map_container_t::iterator, bool> result =
        mappings_.insert({gpu_mapping->gpu_addr(), gpu_mapping});
    DASSERT(result.second);

    mappings_by_buffer_.insert({gpu_mapping->buffer()->platform_buffer(), result.first});

    return true;
}

void AddressSpace::ReleaseBuffer(magma::PlatformBuffer* buffer,
                                 std::vector<std::shared_ptr<GpuMapping>>* released_mappings_out)
{
    released_mappings_out->clear();

    auto range = mappings_by_buffer_.equal_range(buffer);
    for (auto iter = range.first; iter != range.second;) {
        released_mappings_out->emplace_back(std::move(iter->second->second));
        mappings_.erase(iter->second);
        iter = mappings_by_buffer_.erase(iter);
    }
}
