// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_buffer.h"
#include "msd.h"

MsdIntelBuffer::MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
    : platform_buf_(std::move(platform_buf))
{
}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Import(uint32_t handle)
{
    auto platform_buf = magma::PlatformBuffer::Import(handle);
    if (!platform_buf)
        return DRETP(nullptr,
                     "MsdIntelBuffer::Create: Could not create platform buffer from token");

    return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

std::unique_ptr<MsdIntelBuffer> MsdIntelBuffer::Create(uint64_t size)
{
    auto platform_buf = magma::PlatformBuffer::Create(size);
    if (!platform_buf)
        return DRETP(nullptr, "MsdIntelBuffer::Create: Could not create platform buffer from size");

    return std::unique_ptr<MsdIntelBuffer>(new MsdIntelBuffer(std::move(platform_buf)));
}

bool MsdIntelBuffer::MapGpu(AddressSpace* address_space, uint32_t alignment)
{
    DASSERT(!mapping_);

    if (alignment == 0)
        alignment = PAGE_SIZE;

    uint64_t align_pow2;
    if (!magma::get_pow2(alignment, &align_pow2))
        return DRETF(false, "alignment is not power of 2");

    // Casting to uint8_t below
    DASSERT((align_pow2 & ~0xFF) == 0);

    uint64_t size = platform_buffer()->size();
    if (size > address_space->Size())
        return DRETF(false, "buffer size (%lx) greater than address space size (%lx)", platform_buffer()->size(), address_space->Size());

    DASSERT(magma::is_page_aligned(size));

    if (!platform_buffer()->PinPages(0, size / PAGE_SIZE))
        return DRETF(false, "failed to pin pages");

    gpu_addr_t gpu_addr;
    if (!address_space->Alloc(size, static_cast<uint8_t>(align_pow2), &gpu_addr))
        return DRETF(false, "failed to allocate gpu address");

    DLOG("MapGpu alignment 0x%x (pow2 0x%x) allocated gpu_addr 0x%llx", alignment,
         static_cast<uint32_t>(align_pow2), gpu_addr);

    if (!address_space->Insert(gpu_addr, platform_buffer(), caching_type()))
        return DRETF(false, "failed to insert into address_space");

    mapping_ = std::unique_ptr<GpuMapping>(new GpuMapping{address_space->id(), gpu_addr});

    return true;
}

bool MsdIntelBuffer::UnmapGpu(AddressSpace* address_space)
{
    DASSERT(mapping_);
    DASSERT(mapping_->address_space_id == address_space->id());

    bool ret = true;

    if (!platform_buffer()->UnpinPages(0, platform_buffer()->size() / PAGE_SIZE)) {
        DLOG("failed to unpin pages");
        ret = false;
    }

    if (!address_space->Clear(mapping_->addr)) {
        DLOG("failed to clear address");
        ret = false;
    }

    if (!address_space->Free(mapping_->addr)) {
        DLOG("failed to free address");
        ret = false;
    }

    mapping_.release();

    return DRETF(ret, "error occured while unpinning");
}

bool MsdIntelBuffer::GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* addr_out)
{
    DASSERT(addr_out);

    if (!mapping_)
        return DRETF(false, "no mapping");

    if (mapping_->address_space_id != address_space_id)
        return DRETF(false, "incorrect address space");

    *addr_out = mapping_->addr;

    return true;
}

//////////////////////////////////////////////////////////////////////////////

msd_buffer* msd_buffer_import(uint32_t handle)
{
    auto buffer = MsdIntelBuffer::Import(handle);
    if (!buffer)
        return DRETP(nullptr, "MsdIntelBuffer::Create failed");
    return new MsdIntelAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer* buf) { delete MsdIntelAbiBuffer::cast(buf); }
