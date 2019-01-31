// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slice-extent.h"

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>

namespace fvm {

fbl::unique_ptr<SliceExtent> SliceExtent::Split(size_t vslice) {
    ZX_DEBUG_ASSERT(start() <= vslice);
    ZX_DEBUG_ASSERT(vslice < end());
    fbl::AllocChecker ac;
    fbl::unique_ptr<SliceExtent> new_extent(new (&ac) SliceExtent(vslice + 1));
    if (!ac.check()) {
        return nullptr;
    }
    new_extent->pslices_.reserve(end() - vslice, &ac);
    if (!ac.check()) {
        return nullptr;
    }
    for (size_t vs = vslice + 1; vs < end(); vs++) {
        ZX_ASSERT(new_extent->push_back(get(vs)));
    }
    while (!is_empty() && vslice + 1 != end()) {
        pop_back();
    }
    return new_extent;
}

bool SliceExtent::Merge(const SliceExtent& other) {
    ZX_DEBUG_ASSERT(end() == other.start());
    fbl::AllocChecker ac;
    pslices_.reserve(other.size(), &ac);
    if (!ac.check()) {
        return false;
    }

    for (size_t vs = other.start(); vs < other.end(); vs++) {
        ZX_ASSERT(push_back(other.get(vs)));
    }
    return true;
}

} // namespace fvm
