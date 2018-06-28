// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_BUFFER_H
#define MSD_INTEL_BUFFER_H

#include "magma_util/macros.h"
#include "msd.h"
#include "platform_buffer.h"
#include "platform_event.h"
#include "types.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class GpuMapping;
class AddressSpace;

class MsdIntelBuffer {
public:
    static std::unique_ptr<MsdIntelBuffer> Import(uint32_t handle);
    static std::unique_ptr<MsdIntelBuffer> Create(uint64_t size, const char* name);

    magma::PlatformBuffer* platform_buffer()
    {
        DASSERT(platform_buf_);
        return platform_buf_.get();
    }

    // Retains a weak reference to the given mapping so it can be reused.
    std::shared_ptr<GpuMapping> ShareBufferMapping(std::unique_ptr<GpuMapping> mapping);

    // Returns exact match mappings only.
    std::shared_ptr<GpuMapping> FindBufferMapping(std::shared_ptr<AddressSpace> address_space,
                                                  uint64_t offset, uint64_t length);

    // Returns a vector containing retained mappings for the given address space.
    std::vector<std::shared_ptr<GpuMapping>> GetSharedMappings(AddressSpace* address_space);

    // Removes the given |mapping| from the retained mappings list.
    void RemoveSharedMapping(GpuMapping* mapping);

    uint32_t shared_mapping_count() { return shared_mappings_.size(); }

private:
    MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

    std::unique_ptr<magma::PlatformBuffer> platform_buf_;
    std::unordered_map<GpuMapping*, std::weak_ptr<GpuMapping>> shared_mappings_;
};

class MsdIntelAbiBuffer : public msd_buffer_t {
public:
    MsdIntelAbiBuffer(std::shared_ptr<MsdIntelBuffer> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiBuffer* cast(msd_buffer_t* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdIntelAbiBuffer*>(buf);
    }
    std::shared_ptr<MsdIntelBuffer> ptr() { return ptr_; }

private:
    std::shared_ptr<MsdIntelBuffer> ptr_;
    static const uint32_t kMagic = 0x62756666; // "buff"
};

#endif // MSD_INTEL_BUFFER_H
