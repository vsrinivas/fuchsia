// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/limits.h>

#include "pinned-vmo.h"

namespace audio {
namespace intel_hda {

zx_status_t PinnedVmo::Pin(const zx::vmo& vmo, const zx::bti& bti, uint32_t rights) {
    // If we are holding a pinned memory token, then we are already holding a
    // pinned VMO.  It is an error to try and pin a new VMO without first
    // explicitly unpinning the old one.
    if (pmt_.is_valid()) {
        ZX_DEBUG_ASSERT(regions_ != nullptr);
        ZX_DEBUG_ASSERT(region_count_ > 0);
        return ZX_ERR_BAD_STATE;
    }

    // Check our args, read/write is all that users may ask for.
    constexpr uint32_t kAllowedRights = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
    if (((rights & kAllowedRights) != rights) || !vmo.is_valid() || !bti.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Before proceeding, we need to know how big the VMO we are pinning is.
    zx_status_t res;
    uint64_t vmo_size;
    res = vmo.get_size(&vmo_size);
    if (res != ZX_OK) {
        return res;
    }

    // Allocate storage for the results.
    ZX_DEBUG_ASSERT((vmo_size > 0) && !(vmo_size & (PAGE_SIZE - 1)));
    ZX_DEBUG_ASSERT((vmo_size / PAGE_SIZE) < fbl::numeric_limits<uint32_t>::max());
    fbl::AllocChecker ac;
    uint32_t page_count = static_cast<uint32_t>(vmo_size / PAGE_SIZE);
    fbl::unique_ptr<zx_paddr_t[]> addrs(new (&ac) zx_paddr_t[page_count]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Now actually pin the region.
    res = bti.pin(rights, vmo, 0, vmo_size, addrs.get(), page_count, &pmt_);
    if (res != ZX_OK) {
        return res;
    }

    // From here on out, if anything goes wrong, we need to make sure to clean
    // up.  Setup an autocall to take care of this for us.
    auto cleanup = fbl::MakeAutoCall([&]() { UnpinInternal(); });

    // Do a quick pass over the pages to figure out how many adjacent pages we
    // can merge.  This will let us know how many regions we will need storage
    // for our regions array.
    zx_paddr_t last = addrs[0];
    region_count_ = 1;
    for (uint32_t i = 1; i < page_count; ++i) {
        if (addrs[i] != (last + PAGE_SIZE)) {
            ++region_count_;
        }
        last = addrs[i];
    }

    // Allocate storage for our regions.
    regions_.reset(new (&ac) Region[region_count_]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Finally, go ahead and merge any adjacent pages to compute our set of
    // regions and we should be good to go;
    regions_[0].phys_addr = addrs[0];
    regions_[0].size = PAGE_SIZE;
    for (uint32_t i = 1, j = 0; i < page_count; ++i) {
        ZX_DEBUG_ASSERT(j < region_count_);

        if ((regions_[j].phys_addr + regions_[j].size) == addrs[i]) {
            // Merge!
            regions_[j].size += PAGE_SIZE;
        } else {
            // New Region!
            ++j;
            ZX_DEBUG_ASSERT(j < region_count_);
            regions_[j].phys_addr = addrs[i];
            regions_[j].size = PAGE_SIZE;
        }
    }

    cleanup.cancel();
    return ZX_OK;
}

void PinnedVmo::Unpin() {
    if (!pmt_.is_valid()) {
        ZX_DEBUG_ASSERT(regions_ == nullptr);
        ZX_DEBUG_ASSERT(region_count_ == 0);
        return;
    }

    ZX_DEBUG_ASSERT(regions_ != nullptr);
    ZX_DEBUG_ASSERT(region_count_ > 0);

    UnpinInternal();
}

void PinnedVmo::UnpinInternal() {
    ZX_DEBUG_ASSERT(pmt_.is_valid());

    // Given the level of sanity checking we have done so far, it should be
    // completely impossible for us to fail to unpin this memory.
    __UNUSED zx_status_t res;
    res = pmt_.unpin();
    ZX_DEBUG_ASSERT(res == ZX_OK);

    regions_.reset();
    region_count_ = 0;
}

}  // namespace intel_hda
}  // namespace audio
