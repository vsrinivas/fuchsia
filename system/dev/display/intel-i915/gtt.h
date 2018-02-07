// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>
#include <zx/vmo.h>

namespace i915 {

class Controller;
class Gtt;

class GttRegion {
public:
    GttRegion(fbl::unique_ptr<const RegionAllocator::Region> region, Gtt* gtt);
    ~GttRegion();

    uint64_t base() const { return region_->base; }
    uint64_t size() const { return region_->size; }

private:
    fbl::unique_ptr<const RegionAllocator::Region> region_;
    Gtt* gtt_;
};

class Gtt {
public:
    Gtt();
    zx_status_t Init(Controller* controller);
    fbl::unique_ptr<const GttRegion> Insert(zx::vmo* buffer,
                                            uint32_t length, uint32_t align_pow2,
                                            uint32_t pte_padding);
    void SetupForMexec(uintptr_t stolen_fb, uint32_t length, uint32_t pte_padding);
private:
    Controller* controller_;
    RegionAllocator region_allocator_;
    zx::vmo scratch_buffer_;
    zx_paddr_t scratch_buffer_paddr_;

    friend class GttRegion;
};

} // namespace i915
