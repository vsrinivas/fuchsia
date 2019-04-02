// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include "magma_util/macros.h"
#include "platform_bus_mapper.h"
#include "types.h"
#include <memory>
#include <vector>

class AddressSpace;
class MsdIntelBuffer;

// GpuMappingView exposes a non-mutable interface to a GpuMapping.
class GpuMappingView {
public:
    GpuMappingView(std::shared_ptr<MsdIntelBuffer> buffer, gpu_addr_t gpu_addr, uint64_t offset,
                   uint64_t length);

    gpu_addr_t gpu_addr() const { return gpu_addr_; }

    uint64_t offset() const { return offset_; }

    // Length of a GpuMapping is mutable; this method is racy if called from a thread other
    // than the connection thread.
    uint64_t length() const { return length_; }

    uint64_t BufferId() const;
    uint64_t BufferSize() const;

    bool Copy(std::vector<uint32_t>* buffer_out) const;

protected:
    ~GpuMappingView() {}

    std::shared_ptr<MsdIntelBuffer> buffer_;
    const gpu_addr_t gpu_addr_;
    const uint64_t offset_;
    uint64_t length_;
};

// GpuMapping is created by the connection thread, and mutated only by the connection thread.
// However, shared references to GpuMapping are taken by command buffers, keeping them alive while
// the mappings are in flight.
// Therefore, GpuMappings can be destroyed from the device thread, if the connection has removed
// all its references.
// Mutation of the page tables in an AddressSpace is therefore thread locked.
class GpuMapping : public GpuMappingView {
public:
    GpuMapping(std::shared_ptr<AddressSpace> address_space, std::shared_ptr<MsdIntelBuffer> buffer,
               uint64_t offset, uint64_t length, gpu_addr_t gpu_addr,
               std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping);

    ~GpuMapping() { Release(nullptr); }

    MsdIntelBuffer* buffer() { return buffer_.get(); }

    std::weak_ptr<AddressSpace> address_space() const { return address_space_; }

    // Add the given |bus_mapping|.
    // Note that length() changes as a result.
    void Grow(std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping)
    {
        length_ += bus_mapping->page_count() * magma::page_size();
        bus_mappings_.emplace_back(std::move(bus_mapping));
    }

    // Releases the gpu mapping, returns all bus mappings in |bus_mappings_out|.
    // Called by the device thread (via destructor), or connection thread.
    void
    Release(std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>* bus_mappings_out);

private:
    std::weak_ptr<AddressSpace> address_space_;
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings_;
};

#endif // GPU_MAPPING_H
