// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include "magma_util/macros.h"
#include "platform_bus_mapper.h"
#include "types.h"
#include <memory>

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
    // than the owning connection thread.
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

class GpuMapping : public GpuMappingView {
public:
    GpuMapping(std::shared_ptr<AddressSpace> address_space, std::shared_ptr<MsdIntelBuffer> buffer,
               uint64_t offset, uint64_t length, gpu_addr_t gpu_addr,
               std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping);

    ~GpuMapping() { Release(); }

    // Releases the gpu mapping, returns the bus mapping.
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> Release();

    MsdIntelBuffer* buffer() { return buffer_.get(); }

    std::weak_ptr<AddressSpace> address_space() const { return address_space_; }

private:
    std::weak_ptr<AddressSpace> address_space_;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping_;
};

#endif // GPU_MAPPING_H
