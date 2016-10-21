// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_buffer.h"
#include "gpu_mapping.h"
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

std::shared_ptr<GpuMapping> MsdIntelBuffer::ShareBufferMapping(std::unique_ptr<GpuMapping> mapping)
{
    if (mapping->buffer() != this)
        return DRETP(nullptr, "incorrect buffer");

    std::shared_ptr<GpuMapping> shared_mapping = std::move(mapping);

    mapping_list_.push_back(shared_mapping);

    return shared_mapping;
}

std::shared_ptr<GpuMapping> MsdIntelBuffer::FindBufferMapping(AddressSpaceId id)
{
    for (auto weak_mapping : mapping_list_) {
        std::shared_ptr<GpuMapping> shared_mapping = weak_mapping.lock();
        DASSERT(shared_mapping);
        if (shared_mapping->address_space_id() == id)
            return shared_mapping;
    }

    return nullptr;
}

void MsdIntelBuffer::RemoveExpiredMappings()
{
    for (auto iter = mapping_list_.begin(); iter != mapping_list_.end();) {
        auto mapping = *iter;
        if (!mapping.lock()) {
            iter = mapping_list_.erase(iter);
        } else {
            iter++;
        }
    }
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
