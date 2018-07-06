// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_BUFFER_H
#define MSD_ARM_BUFFER_H

#include <unordered_set>

#include "magma_util/macros.h"
#include "msd.h"
#include "platform_buffer.h"
#include "platform_event.h"

class GpuMapping;

// This can only be accessed on the connection thread.
class MsdArmBuffer {
public:
    static std::unique_ptr<MsdArmBuffer> Import(uint32_t handle);
    static std::unique_ptr<MsdArmBuffer> Create(uint64_t size, const char* name);

    ~MsdArmBuffer();

    magma::PlatformBuffer* platform_buffer()
    {
        DASSERT(platform_buf_);
        return platform_buf_.get();
    }

    void AddMapping(GpuMapping* mapping);
    void RemoveMapping(GpuMapping* mapping);

    bool SetCommittedPages(uint64_t start_page, uint64_t pages);
    uint64_t start_committed_pages() const { return start_committed_pages_; }
    uint64_t committed_page_count() const { return committed_page_count_; }

private:
    MsdArmBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

    std::unique_ptr<magma::PlatformBuffer> platform_buf_;

    std::unordered_set<GpuMapping*> gpu_mappings_;
    uint64_t start_committed_pages_ = {};
    uint64_t committed_page_count_ = {};
};

class MsdArmAbiBuffer : public msd_buffer_t {
public:
    MsdArmAbiBuffer(std::shared_ptr<MsdArmBuffer> ptr) : base_ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdArmAbiBuffer* cast(msd_buffer_t* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdArmAbiBuffer*>(buf);
    }

    std::shared_ptr<MsdArmBuffer> base_ptr() { return base_ptr_; }

private:
    std::shared_ptr<MsdArmBuffer> base_ptr_;

    static const uint32_t kMagic = 0x62756666; // "buff"
};

#endif // MSD_ARM_BUFFER_H
