// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping.h"
#include "address_space.h"
#include "msd_intel_buffer.h"

GpuMapping::GpuMapping(std::shared_ptr<AddressSpace> address_space,
                       std::shared_ptr<MsdIntelBuffer> buffer, uint64_t offset, uint64_t length,
                       gpu_addr_t gpu_addr)
    : address_space_(address_space), buffer_(buffer), offset_(offset), length_(length),
      gpu_addr_(gpu_addr)
{
}

GpuMapping::~GpuMapping()
{
    if (!buffer_->platform_buffer()->UnpinPages(offset_ / PAGE_SIZE, length_ / PAGE_SIZE))
        DLOG("failed to unpin pages");

    buffer_->RemoveSharedMapping(this);

    std::shared_ptr<AddressSpace> address_space = address_space_.lock();
    if (!address_space) {
        DLOG("Failed to lock address space");
        return;
    }

    if (!address_space->Clear(gpu_addr_))
        DLOG("failed to clear address");

    if (!address_space->Free(gpu_addr_))
        DLOG("failed to free address");
}
