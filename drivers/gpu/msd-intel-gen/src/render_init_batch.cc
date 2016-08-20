// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "render_init_batch.h"
#include "magma_util/macros.h"

static inline uint32_t read_src(const uint32_t* batch, uint32_t offset, uint32_t batch_size)
{
    DASSERT(offset < batch_size);
    DASSERT((offset & 0x3) == 0);
    return batch[offset >> 2];
}

static inline void write_dst(uint32_t val, void* dest, uint32_t offset, uint32_t dest_size)
{
    DASSERT(offset < dest_size);
    DASSERT((offset & 0x3) == 0);
    reinterpret_cast<uint32_t*>(dest)[offset >> 2] = val;
}

bool RenderInitBatch::Init(std::unique_ptr<MsdIntelBuffer> buffer, AddressSpace* address_space)
{
    uint32_t batch_size = Size();
    DASSERT((batch_size & 0x3) == 0);

    DLOG("RenderInitBatch size 0x%x", batch_size);

    if (buffer->platform_buffer()->size() < batch_size)
        return DRETF(false, "buffer too small");

    if (!buffer->MapGpu(address_space, PAGE_SIZE))
        return DRETF(false, "failed to pin buffer");

    uint64_t gpu_addr;
    if (!buffer->GetGpuAddress(address_space->id(), &gpu_addr))
        return DRETF(false, "failed to get gpu address");

    DASSERT(buffer->write_domain() == MEMORY_DOMAIN_CPU);

    void* dst;
    if (!buffer->platform_buffer()->MapCpu(&dst))
        return DRETF(false, "failed to map buffer");

    memcpy(dst, batch_, batch_size);

    for (unsigned int i = 0; i < RenderInitBatch::RelocationCount(); i++) {
        uint32_t offset = relocs_[i];
        uint32_t val = read_src(batch_, offset, batch_size);
        uint64_t reloc = val + gpu_addr;
        DLOG("writing reloc 0x%llx offset 0x%x", reloc, offset);
        write_dst(magma::lower_32_bits(reloc), dst, offset, buffer->platform_buffer()->size());
        write_dst(magma::upper_32_bits(reloc), dst, offset + 4, buffer->platform_buffer()->size());
    }

    if (!buffer->platform_buffer()->UnmapCpu())
        DLOG("failed to unmap buffer");

    // Assume ownership.
    buffer_ = std::move(buffer);

    return true;
}

bool RenderInitBatch::GetGpuAddress(AddressSpaceId id, gpu_addr_t* addr_out)
{
    if (!buffer_)
        return DRETF(false, "no buffer");

    if (!buffer_->GetGpuAddress(id, addr_out))
        return DRETF(false, "failed to get gpu address");

    return true;
}
