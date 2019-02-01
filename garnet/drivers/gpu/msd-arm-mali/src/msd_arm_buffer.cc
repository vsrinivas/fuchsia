// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_buffer.h"

#include "gpu_mapping.h"
#include "msd.h"
#include "msd_arm_connection.h"

MsdArmBuffer::MsdArmBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
    : platform_buf_(std::move(platform_buf))
{
}

MsdArmBuffer::~MsdArmBuffer()
{
    size_t mapping_count = gpu_mappings_.size();
    for (auto mapping : gpu_mappings_)
        mapping->Remove();
    // The weak pointer to this should already have been invalidated, so
    // Remove() shouldn't be able to modify gpu_mappings_.
    DASSERT(gpu_mappings_.size() == mapping_count);
}

std::unique_ptr<MsdArmBuffer> MsdArmBuffer::Import(uint32_t handle)
{
    auto platform_buf = magma::PlatformBuffer::Import(handle);
    if (!platform_buf)
        return DRETP(nullptr, "MsdArmBuffer::Create: Could not create platform buffer from token");

    return std::unique_ptr<MsdArmBuffer>(new MsdArmBuffer(std::move(platform_buf)));
}

std::unique_ptr<MsdArmBuffer> MsdArmBuffer::Create(uint64_t size, const char* name)
{
    auto platform_buf = magma::PlatformBuffer::Create(size, name);
    if (!platform_buf)
        return DRETP(nullptr, "MsdArmBuffer::Create: Could not create platform buffer from size");

    return std::unique_ptr<MsdArmBuffer>(new MsdArmBuffer(std::move(platform_buf)));
}

void MsdArmBuffer::AddMapping(GpuMapping* mapping)
{
    DASSERT(!gpu_mappings_.count(mapping));
    gpu_mappings_.insert(mapping);
}

void MsdArmBuffer::RemoveMapping(GpuMapping* mapping)
{
    DASSERT(gpu_mappings_.count(mapping));
    gpu_mappings_.erase(mapping);
}

bool MsdArmBuffer::SetCommittedPages(uint64_t start_page, uint64_t page_count)
{
    if ((start_page + page_count) * PAGE_SIZE > platform_buffer()->size())
        return DRETF(false, "invalid parameters start_page %lu page_count %lu\n", start_page,
                     page_count);

    start_committed_pages_ = start_page;
    committed_page_count_ = page_count;
    bool success = true;
    for (auto& mapping : gpu_mappings_) {
        if (!mapping->UpdateCommittedMemory())
            success = false;
    }
    return success;
}

bool MsdArmBuffer::EnsureRegionFlushed(uint64_t start_bytes, uint64_t end_bytes)
{
    DASSERT(end_bytes >= start_bytes);
    DASSERT(flushed_region_end_bytes_ >= flushed_region_start_bytes_);
    if (start_bytes < flushed_region_start_bytes_) {
        if (!platform_buf_->CleanCache(start_bytes, flushed_region_start_bytes_ - start_bytes,
                                       false))
            return DRETF(false, "CleanCache of start failed");

        flushed_region_start_bytes_ = start_bytes;
    }

    if (end_bytes > flushed_region_end_bytes_) {
        bool region_exists = flushed_region_end_bytes_ != 0;
        uint64_t flush_start;
        if (region_exists) {
            flush_start = flushed_region_end_bytes_;
        } else {
            flush_start = start_bytes;
            flushed_region_start_bytes_ = flush_start;
        }

        if (!platform_buf_->CleanCache(flush_start, end_bytes - flush_start, false))
            return DRETF(false, "CleanCache of end failed");
        flushed_region_end_bytes_ = end_bytes;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////

msd_buffer_t* msd_buffer_import(uint32_t handle)
{
    auto buffer = MsdArmBuffer::Import(handle);
    if (!buffer)
        return DRETP(nullptr, "MsdArmBuffer::Create failed");
    return new MsdArmAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer_t* buf) { delete MsdArmAbiBuffer::cast(buf); }
