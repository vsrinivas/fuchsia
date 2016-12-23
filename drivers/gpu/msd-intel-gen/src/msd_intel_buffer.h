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
#include <list>
#include <memory>
#include <mutex>

class GpuMapping;

class MsdIntelBuffer {
public:
    static std::unique_ptr<MsdIntelBuffer> Import(uint32_t handle);
    static std::unique_ptr<MsdIntelBuffer> Create(uint64_t size);

    magma::PlatformBuffer* platform_buffer()
    {
        DASSERT(platform_buf_);
        return platform_buf_.get();
    }

    uint32_t read_domains() { return read_domains_bitfield_; }

    uint32_t write_domain() { return write_domain_bitfield_; }

    // connection thread
    void IncrementInflightCounter() { ++inflight_counter_; }

    // device thread
    void DecrementInflightCounter();

    // connection thread
    void WaitRendering();

    uint32_t inflight_counter() { return inflight_counter_ & 0xFFFFFFFF; }

    CachingType caching_type() { return caching_type_; }

    std::shared_ptr<GpuMapping> ShareBufferMapping(std::unique_ptr<GpuMapping> mapping);

    // Returns exact match mappings only.
    std::shared_ptr<GpuMapping> FindBufferMapping(AddressSpaceId id, uint64_t offset,
                                                  uint64_t length, uint32_t alignment);

    void RemoveExpiredMappings();

    uint32_t shared_mapping_count() { return mapping_list_.size(); }

private:
    MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

    std::unique_ptr<magma::PlatformBuffer> platform_buf_;

    CachingType caching_type_ = CACHING_LLC;

    uint32_t read_domains_bitfield_ = MEMORY_DOMAIN_CPU;
    uint32_t write_domain_bitfield_ = MEMORY_DOMAIN_CPU;

    std::atomic_uint64_t inflight_counter_{};
    std::unique_ptr<magma::PlatformEvent> wait_rendering_event_;
    std::mutex wait_rendering_mutex_;

    std::list<std::weak_ptr<GpuMapping>> mapping_list_;
};

class MsdIntelAbiBuffer : public msd_buffer {
public:
    MsdIntelAbiBuffer(std::shared_ptr<MsdIntelBuffer> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdIntelAbiBuffer* cast(msd_buffer* buf)
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
