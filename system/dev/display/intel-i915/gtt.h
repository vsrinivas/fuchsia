// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>
#include <zx/vmo.h>

namespace i915 {

class Device;
using GttRegion = RegionAllocator::Region;

class Gtt {
public:
    Gtt();
    zx_status_t Init(hwreg::RegisterIo* mmio_space, uint32_t gtt_size);
    fbl::unique_ptr<const GttRegion> Insert(hwreg::RegisterIo* mmio_space, zx::vmo* buffer,
                                            uint32_t length, uint32_t align_pow2,
                                            uint32_t pte_padding);
private:
    RegionAllocator region_allocator_;
};

} // namespace i915
