// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping.h"
#include "address_space.h"
#include "msd_intel_buffer.h"

GpuMapping::GpuMapping(std::shared_ptr<AddressSpace> address_space,
                       std::shared_ptr<MsdIntelBuffer> buffer, gpu_addr_t gpu_addr)
    : address_space_(address_space), buffer_(buffer), address_space_id_(address_space->id()),
      gpu_addr_(gpu_addr)
{
}

GpuMapping::~GpuMapping()
{
    if (!buffer_->platform_buffer()->UnpinPages(0, buffer_->platform_buffer()->size() / PAGE_SIZE))
        DLOG("failed to unpin pages");

    buffer_->RemoveExpiredMappings();

    std::shared_ptr<AddressSpace> address_space = address_space_.lock();
    DASSERT(address_space);

    if (address_space && !address_space->Clear(gpu_addr_))
        DLOG("failed to clear address");

    if (address_space && !address_space->Free(gpu_addr_))
        DLOG("failed to free address");
}
