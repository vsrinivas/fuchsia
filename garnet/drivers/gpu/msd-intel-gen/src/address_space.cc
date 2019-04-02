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

    if (gpu_addr + page_count * PAGE_SIZE > address_space->Size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "gpu_addr 0x%lx + page_count (%lu) > address space size (0x%lx)", gpu_addr,
                        page_count, address_space->Size());

    magma::PlatformBuffer* platform_buffer = buffer->platform_buffer();

    if ((page_offset + page_count) * PAGE_SIZE > platform_buffer->size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "page_offset (%lu) + page_count (%lu) > buffer size (0x%lx)", page_offset,
                        page_count, platform_buffer->size());

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
                                                         uint64_t offset, uint64_t length) const
{
    DASSERT(buffer);

    auto range = mappings_by_buffer_.equal_range(buffer->platform_buffer());
    for (auto iter = range.first; iter != range.second; iter++) {
        auto& mapping = iter->second->second;
        if (mapping->offset() == offset && mapping->length() >= GetMappedSize(length))
            return mapping;
    }

    return nullptr;
}

std::shared_ptr<GpuMapping> AddressSpace::FindGpuMapping(uint64_t gpu_addr) const
{
    auto iter = mappings_.find(gpu_addr);
    return (iter != mappings_.end()) ? iter->second : nullptr;
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

bool AddressSpace::ReleaseMapping(magma::PlatformBuffer* buffer, gpu_addr_t gpu_addr,
                                  std::shared_ptr<GpuMapping>* mapping_out)
{
    auto range = mappings_by_buffer_.equal_range(buffer);
    for (auto iter = range.first; iter != range.second; iter++) {
        std::shared_ptr<GpuMapping> gpu_mapping = iter->second->second;
        if (gpu_mapping->gpu_addr() == gpu_addr) {
            mappings_.erase(iter->second);
            mappings_by_buffer_.erase(iter);
            *mapping_out = std::move(gpu_mapping);
            return true;
        }
    }
    return DRETF(false, "failed to remove mapping");
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

magma::Status AddressSpace::GrowMapping(GpuMapping* mapping, uint64_t page_increment)
{
    const uint64_t length = mapping->length() + page_increment * PAGE_SIZE;

    if (mapping->gpu_addr() + length > Size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "gpu_addr 0x%lx + length %lu > address space size (0x%lx)",
                        mapping->gpu_addr(), length, Size());

    auto iter = mappings_.upper_bound(mapping->gpu_addr());
    if (iter != mappings_.end() && (mapping->gpu_addr() + length > iter->second->gpu_addr()))
        return DRETF(false, "Mapping overlaps existing mapping");

    magma::PlatformBuffer* platform_buffer = mapping->buffer()->platform_buffer();

    if (mapping->offset() + length > platform_buffer->size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                        "offset (%lu) + length (%lu) > buffer size (0x%lx)", mapping->offset(),
                        length, platform_buffer->size());

    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping =
        owner_->GetBusMapper()->MapPageRangeBus(
            platform_buffer, (mapping->offset() + mapping->length()) / magma::page_size(),
            page_increment);
    if (!bus_mapping)
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to map page range to bus");

    if (!Insert(mapping->gpu_addr() + mapping->length(), bus_mapping.get()))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to insert into address_space");

    mapping->Grow(std::move(bus_mapping));

    return MAGMA_STATUS_OK;
}
