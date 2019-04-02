// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping.h"
#include "address_space.h"
#include "msd_intel_buffer.h"

GpuMappingView::GpuMappingView(std::shared_ptr<MsdIntelBuffer> buffer, gpu_addr_t gpu_addr,
                               uint64_t offset, uint64_t length)
    : buffer_(std::move(buffer)), gpu_addr_(gpu_addr), offset_(offset), length_(length)
{
}

uint64_t GpuMappingView::BufferId() const { return buffer_->platform_buffer()->id(); }

uint64_t GpuMappingView::BufferSize() const { return buffer_->platform_buffer()->size(); }

bool GpuMappingView::Copy(std::vector<uint32_t>* buffer_out) const
{
    void* data;
    if (!buffer_->platform_buffer()->MapCpu(&data))
        return DRETF(false, "couldn't map buffer");

    buffer_out->resize(buffer_->platform_buffer()->size());
    std::memcpy(buffer_out->data(), data, buffer_out->size());

    buffer_->platform_buffer()->UnmapCpu();
    return true;
}

GpuMapping::GpuMapping(std::shared_ptr<AddressSpace> address_space,
                       std::shared_ptr<MsdIntelBuffer> buffer, uint64_t offset, uint64_t length,
                       gpu_addr_t gpu_addr,
                       std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping)
    : GpuMappingView(std::move(buffer), gpu_addr, offset, length), address_space_(address_space)
{
    bus_mappings_.emplace_back(std::move(bus_mapping));
}

void GpuMapping::Release(
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>* bus_mappings_out)
{
    std::shared_ptr<AddressSpace> address_space = address_space_.lock();
    if (address_space) {
        if (!address_space->Clear(gpu_addr(), length() / PAGE_SIZE))
            DLOG("failed to clear address");

        if (!address_space->Free(gpu_addr()))
            DLOG("failed to free address");
    }

    buffer_.reset();
    address_space_.reset();
    length_ = 0;
    if (bus_mappings_out) {
        *bus_mappings_out = std::move(bus_mappings_);
    }
    bus_mappings_.clear();
}
