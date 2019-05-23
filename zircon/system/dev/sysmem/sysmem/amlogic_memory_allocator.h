// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <lib/zx/bti.h>
#include <region-alloc/region-alloc.h>

#include "protected_memory_allocator.h"

class AmlogicMemoryAllocator : public ProtectedMemoryAllocator {
public:
    AmlogicMemoryAllocator(zx::bti bti);

    zx_status_t Init(uint64_t size);

    zx_status_t Allocate(uint64_t size, zx::vmo* vmo) override;
    bool CoherencyDomainIsInaccessible() override;
    zx_status_t GetProtectedMemoryInfo(uint64_t* base, uint64_t* size) override;

private:
    zx::bti bti_;
    struct Region {
        RegionAllocator::Region::UPtr region;
        zx::vmo vmo;
    };
    RegionAllocator protected_allocator_;
    fbl::Vector<fbl::unique_ptr<Region>> regions_;
    uint64_t start_;
    uint64_t size_;
    zx::vmo contiguous_vmo_;
};
