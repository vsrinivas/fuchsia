// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space.h"

std::unique_ptr<GpuMapping> AddressSpace::MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                       std::shared_ptr<MsdIntelBuffer> buffer,
                                                       uint64_t offset, uint64_t length,
                                                       uint32_t alignment)
{
    DASSERT(address_space);
    DASSERT(buffer);

    if (alignment == 0)
        alignment = PAGE_SIZE;

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
    if (!magma::get_pow2(alignment, &align_pow2))
        return DRETP(nullptr, "alignment is not power of 2");

    // Casting to uint8_t below
    DASSERT((align_pow2 & ~0xFF) == 0);
    DASSERT(magma::is_page_aligned(length));

    if (!buffer->platform_buffer()->PinPages(offset / PAGE_SIZE, length / PAGE_SIZE))
        return DRETP(nullptr, "failed to pin pages");

    gpu_addr_t gpu_addr;
    if (!address_space->Alloc(length, static_cast<uint8_t>(align_pow2), &gpu_addr))
        return DRETP(nullptr, "failed to allocate gpu address");

    DLOG("MapBufferGpu offset 0x%lx length 0x%lx alignment 0x%x (pow2 0x%x) allocated gpu_addr "
         "0x%lx",
         offset, length, alignment, static_cast<uint32_t>(align_pow2), gpu_addr);

    if (!address_space->Insert(gpu_addr, buffer->platform_buffer(), offset, length,
                               buffer->caching_type()))
        return DRETP(nullptr, "failed to insert into address_space");

    return std::unique_ptr<GpuMapping>(
        new GpuMapping(address_space, buffer, offset, length, gpu_addr));
}

std::shared_ptr<GpuMapping>
AddressSpace::GetSharedGpuMapping(std::shared_ptr<AddressSpace> address_space,
                                  std::shared_ptr<MsdIntelBuffer> buffer, uint64_t offset,
                                  uint64_t length, uint32_t alignment)
{
    DASSERT(address_space);
    DASSERT(buffer);

    std::shared_ptr<GpuMapping> mapping =
        buffer->FindBufferMapping(address_space, offset, length, alignment);
    if (!mapping) {
        std::unique_ptr<GpuMapping> new_mapping =
            AddressSpace::MapBufferGpu(address_space, buffer, offset, length, alignment);
        if (!new_mapping)
            return DRETP(nullptr, "Couldn't map buffer to gtt");
        mapping = buffer->ShareBufferMapping(std::move(new_mapping));
    }
    address_space->cache_->CacheMapping(mapping);
    return mapping;
}
