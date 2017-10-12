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
    std::unordered_set<GpuMapping*> mappings;
    // Make a copy, as removing each GpuMapping will call into RemoveMapping,
    // which modifies gpu_mappings_.
    mappings = gpu_mappings_;
    for (auto mapping : mappings) {
        mapping->Remove();
    }
    DASSERT(gpu_mappings_.empty());
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

//////////////////////////////////////////////////////////////////////////////

msd_buffer_t* msd_buffer_import(uint32_t handle)
{
    auto buffer = MsdArmBuffer::Import(handle);
    if (!buffer)
        return DRETP(nullptr, "MsdArmBuffer::Create failed");
    return new MsdArmAbiBuffer(std::move(buffer));
}

void msd_buffer_destroy(msd_buffer_t* buf) { delete MsdArmAbiBuffer::cast(buf); }
