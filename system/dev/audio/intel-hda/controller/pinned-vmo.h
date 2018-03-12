// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <zx/vmo.h>
#include <zircon/types.h>

#include "utils.h"

namespace audio {
namespace intel_hda {

class PinnedVmo {
  public:
    struct Region {
        zx_paddr_t phys_addr;
        uint64_t   size;
    };

    PinnedVmo() = default;
    ~PinnedVmo() { Unpin(); }

    zx_status_t Pin(const zx::vmo& vmo,
                    fbl::RefPtr<RefCountedBti> bti,
                    uint32_t rights);
    void Unpin();

    uint32_t region_count() const { return region_count_; }
    const Region& region(uint32_t ndx) const {
        ZX_DEBUG_ASSERT(ndx < region_count_);
        ZX_DEBUG_ASSERT(regions_ != nullptr);
        return regions_[ndx];
    }

  private:
    void UnpinInternal(zx_paddr_t start_addr);

    fbl::RefPtr<RefCountedBti> bti_;
    fbl::unique_ptr<Region[]> regions_;
    uint32_t region_count_ = 0;
};

}  // namespace intel_hda
}  // namespace audio
