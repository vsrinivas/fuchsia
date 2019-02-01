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

std::unique_ptr<GpuMapping> RenderInitBatch::Init(std::unique_ptr<MsdIntelBuffer> buffer,
                                                  std::shared_ptr<AddressSpace> address_space)
{
    DASSERT((batch_size_ & 0x3) == 0);

    DLOG("RenderInitBatch size 0x%x", batch_size_);

    auto platform_buffer = buffer->platform_buffer();

    if (platform_buffer->size() < batch_size_)
        return DRETP(nullptr, "buffer too small");

    auto mapping = AddressSpace::MapBufferGpu(address_space, std::move(buffer));
    if (!mapping)
        return DRETP(nullptr, "failed to pin buffer");

    void* dst;
    if (!platform_buffer->MapCpu(&dst))
        return DRETP(nullptr, "failed to map buffer");

    memcpy(dst, batch_, batch_size_);

    for (unsigned int i = 0; i < relocation_count_; i++) {
        uint32_t offset = relocs_[i];
        uint32_t val = read_src(batch_, offset, batch_size_);
        uint64_t reloc = val + mapping->gpu_addr();
        DLOG("writing reloc 0x%lx offset 0x%x", reloc, offset);
        write_dst(magma::lower_32_bits(reloc), dst, offset, platform_buffer->size());
        write_dst(magma::upper_32_bits(reloc), dst, offset + 4, platform_buffer->size());
    }

    if (!platform_buffer->UnmapCpu())
        DLOG("failed to unmap buffer");

    return mapping;
}
