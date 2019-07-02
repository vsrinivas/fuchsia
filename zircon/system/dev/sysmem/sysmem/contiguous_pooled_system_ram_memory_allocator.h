// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <lib/zx/bti.h>
#include <region-alloc/region-alloc.h>

#include "allocator.h"

class ContiguousPooledSystemRamMemoryAllocator : public MemoryAllocator {
public:
    ContiguousPooledSystemRamMemoryAllocator(Owner* parent_device, const char* allocation_name,
                                             uint64_t size, bool is_cpu_accessible);

    // Default to page alignment.
    zx_status_t Init(uint32_t alignment_log2 = 12);

    zx_status_t Allocate(uint64_t size, zx::vmo* vmo) override;
    bool CoherencyDomainIsInaccessible() override { return false; }

    zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) override {
        *base = start_;
        *size = size_;
        return ZX_OK;
    }

    const zx::vmo& GetPoolVmoForTest() { return contiguous_vmo_; }

private:
    void DumpPoolStats();
    Owner* const parent_device_{};
    const char* const allocation_name_{};
    zx::vmo contiguous_vmo_;
    struct Region {
        RegionAllocator::Region::UPtr region;
        zx::vmo vmo;
    };
    RegionAllocator region_allocator_;
    fbl::Vector<fbl::unique_ptr<Region>> regions_;
    uint64_t start_{};
    uint64_t size_{};
    bool is_cpu_accessible_{};
};
