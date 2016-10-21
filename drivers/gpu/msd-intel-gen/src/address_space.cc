// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space.h"

std::unique_ptr<GpuMapping> AddressSpace::MapBufferGpu(std::shared_ptr<AddressSpace> address_space,
                                                       std::shared_ptr<MsdIntelBuffer> buffer,
                                                       uint32_t alignment)
{
    if (alignment == 0)
        alignment = PAGE_SIZE;

    uint64_t align_pow2;
    if (!magma::get_pow2(alignment, &align_pow2))
        return DRETP(nullptr, "alignment is not power of 2");

    // Casting to uint8_t below
    DASSERT((align_pow2 & ~0xFF) == 0);

    uint64_t size = buffer->platform_buffer()->size();
    if (size > address_space->Size())
        return DRETP(nullptr, "buffer size greater than address space size");

    DASSERT(magma::is_page_aligned(size));

    if (!buffer->platform_buffer()->PinPages(0, size / PAGE_SIZE))
        return DRETP(nullptr, "failed to pin pages");

    gpu_addr_t gpu_addr;
    if (!address_space->Alloc(size, static_cast<uint8_t>(align_pow2), &gpu_addr))
        return DRETP(nullptr, "failed to allocate gpu address");

    DLOG("MapBufferGpu size %llu alignment 0x%x (pow2 0x%x) allocated gpu_addr 0x%llx",
         buffer->platform_buffer()->size(), alignment, static_cast<uint32_t>(align_pow2), gpu_addr);

    if (!address_space->Insert(gpu_addr, buffer->platform_buffer(), buffer->caching_type()))
        return DRETP(nullptr, "failed to insert into address_space");

    return std::unique_ptr<GpuMapping>(new GpuMapping(address_space, buffer, gpu_addr));
}

std::shared_ptr<GpuMapping>
AddressSpace::GetSharedGpuMapping(std::shared_ptr<AddressSpace> address_space,
                                  std::shared_ptr<MsdIntelBuffer> buffer, uint32_t alignment)
{
    std::shared_ptr<GpuMapping> mapping = buffer->FindBufferMapping(address_space->id());
    if (!mapping) {
        std::unique_ptr<GpuMapping> new_mapping = MapBufferGpu(address_space, buffer, alignment);
        if (!new_mapping)
            return DRETP(nullptr, "Couldn't map buffer to gtt");
        mapping = buffer->ShareBufferMapping(std::move(new_mapping));
    }
    return mapping;
}
