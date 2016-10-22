// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_BUFFER_H
#define MSD_INTEL_BUFFER_H

#include "address_space.h"
#include "magma_util/macros.h"
#include "magma_util/platform/platform_buffer.h"
#include "msd.h"
#include "sequencer.h"
#include "types.h"
#include <map>
#include <memory>

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

    bool MapGpu(AddressSpace* address_space, uint32_t alignment);
    bool UnmapGpu(AddressSpace* address_space);

    bool IsMappedGpu(AddressSpaceId address_space_id)
    {
        // TODO(MA-71) handle multiple address spaces
        return mapping_ != nullptr && mapping_->address_space_id == address_space_id;
    }

    bool GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* addr_out);

    void SetSequenceNumber(uint32_t sequence_number) { sequence_number_ = sequence_number; }

    uint32_t sequence_number() { return sequence_number_; }

private:
    MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

    CachingType caching_type() { return caching_type_; }

    struct GpuMapping {
        AddressSpaceId address_space_id;
        gpu_addr_t addr;
    };

    std::unique_ptr<magma::PlatformBuffer> platform_buf_;
    std::unique_ptr<GpuMapping> mapping_;

    CachingType caching_type_ = CACHING_LLC;

    uint32_t read_domains_bitfield_ = MEMORY_DOMAIN_CPU;
    uint32_t write_domain_bitfield_ = MEMORY_DOMAIN_CPU;

    uint32_t sequence_number_ = Sequencer::kInvalidSequenceNumber;
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
