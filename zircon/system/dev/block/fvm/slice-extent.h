// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus

#include <stdint.h>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fvm/fvm.h>

namespace fvm {

class SliceExtent : public fbl::WAVLTreeContainable<fbl::unique_ptr<SliceExtent>> {
public:
    size_t GetKey() const { return vslice_start_; }
    // Vslice start (inclusive)
    size_t start() const { return vslice_start_; }
    // Vslice end (exclusive)
    size_t end() const { return vslice_start_ + pslices_.size(); }
    // Extent length
    size_t size() const { return end() - start(); }
    // Look up a pslice given a vslice
    uint32_t get(size_t vslice) const {
        size_t offset = vslice - vslice_start_;
        if (offset >= pslices_.size()) {
            return 0;
        }
        return pslices_[offset];
    }

    // Breaks the extent from:
    //   [start(), end())
    // Into:
    //   [start(), vslice] and [vslice + 1, end()).
    // Returns the latter extent on success; returns nullptr
    // if a memory allocation failure occurs.
    fbl::unique_ptr<SliceExtent> Split(size_t vslice);

    // Combines the other extent into this one.
    // 'other' must immediately follow the current slice.
    bool Merge(const SliceExtent& other);

    bool push_back(uint32_t pslice) {
        ZX_DEBUG_ASSERT(pslice != PSLICE_UNALLOCATED);
        fbl::AllocChecker ac;
        pslices_.push_back(pslice, &ac);
        return ac.check();
    }
    void pop_back() { pslices_.pop_back(); }
    bool is_empty() const { return pslices_.size() == 0; }

    SliceExtent(size_t vslice_start) : vslice_start_(vslice_start) {}

private:
    friend class TypeWAVLTraits;
    DISALLOW_COPY_ASSIGN_AND_MOVE(SliceExtent);

    fbl::Vector<uint32_t> pslices_;
    const size_t vslice_start_;
};

} // namespace fvm

#endif // __cplusplus
