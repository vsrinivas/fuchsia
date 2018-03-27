// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_BUFFER_H
#define ZIRCON_PLATFORM_BUFFER_H

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_object.h"
#include "zircon_platform_handle.h"
#include <limits.h> // PAGE_SIZE
#include <vector>
#include <zx/vmar.h>
#include <zx/vmo.h>

namespace magma {

class ZirconPlatformBuffer : public PlatformBuffer {
public:
    ZirconPlatformBuffer(zx::vmo vmo, uint64_t size) : vmo_(std::move(vmo)), size_(size)
    {
        DLOG("ZirconPlatformBuffer ctor size %ld vmo 0x%x", size, vmo_.get());
        DASSERT(magma::is_page_aligned(size));

        bool success = PlatformObject::IdFromHandle(vmo_.get(), &koid_);
        DASSERT(success);
    }

    ~ZirconPlatformBuffer() override
    {
        if (map_count_ > 0)
            vmar_unmap();
    }

    // PlatformBuffer implementation
    uint64_t size() const override { return size_; }

    uint64_t id() const override { return koid_; }

    zx_handle_t handle() const { return vmo_.get(); }

    bool duplicate_handle(uint32_t* handle_out) const override
    {
        zx::vmo duplicate;
        zx_status_t status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
        if (status < 0)
            return DRETF(false, "zx_handle_duplicate failed");
        *handle_out = duplicate.release();
        return true;
    }

    // PlatformBuffer implementation
    bool CommitPages(uint32_t start_page_index, uint32_t page_count) const override;
    bool MapCpu(void** addr_out, uintptr_t alignment) override;
    bool UnmapCpu() override;
    bool MapAtCpuAddr(uint64_t addr) override;

    class ZirconBusMapping : public BusMapping {
    public:
        ZirconBusMapping(uint64_t page_offset, uint64_t page_count)
            : page_offset_(page_offset), page_addr_(page_count)
        {
        }

        uint64_t page_offset() override { return page_offset_; }
        uint64_t page_count() override { return page_addr_.size(); }
        std::vector<uint64_t>& Get() override { return page_addr_; }

    private:
        uint64_t page_offset_;
        std::vector<uint64_t> page_addr_;
    };

    std::unique_ptr<PlatformBuffer::BusMapping> MapPageRangeBus(uint32_t start_page_index,
                                                                uint32_t page_count) override;

    bool CleanCache(uint64_t offset, uint64_t size, bool invalidate) override;
    bool SetCachePolicy(magma_cache_policy_t cache_policy) override;

    uint32_t num_pages() { return size_ / PAGE_SIZE; }

private:
    zx_status_t vmar_unmap()
    {
        zx_status_t status = vmar_.destroy();
        vmar_.reset();

        if (status == ZX_OK)
            virt_addr_ = nullptr;
        return status;
    }

    zx::vmo vmo_;
    zx::vmar vmar_;
    uint64_t size_;
    uint64_t koid_;
    void* virt_addr_{};
    uint32_t map_count_ = 0;
};

} // namespace magma

#endif