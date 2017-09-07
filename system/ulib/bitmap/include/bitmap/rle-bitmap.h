// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/bitmap.h>

#include <stddef.h>

#include <magenta/types.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>

namespace bitmap {

struct RleBitmapElement;

// A run-length encoded bitmap.
class RleBitmap final : public Bitmap {
private:
    // Private forward-declaration to share the type between the iterator type
    // and the internal list.
    using ListType = fbl::DoublyLinkedList<fbl::unique_ptr<RleBitmapElement>>;

public:
    using const_iterator = ListType::const_iterator;
    using FreeList = ListType;

    constexpr RleBitmap()
        : num_elems_(0) {}
    virtual ~RleBitmap() = default;

    RleBitmap(RleBitmap&& rhs) = default;
    RleBitmap& operator=(RleBitmap&& rhs) = default;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(RleBitmap);

    // Returns the current number of ranges.
    size_t num_ranges() const { return num_elems_; }

    // Returns true if all the bits in [*bitoff*, *bitmax*) are set.  Afterwards,
    // *first_unset* will be set to the lesser of bitmax and the index of the
    // first unset bit after *bitoff*.
    bool Get(size_t bitoff, size_t bitmax, size_t* first_unset = nullptr) const override;

    // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails on allocation
    // error or if bitmax < bitoff.
    mx_status_t Set(size_t bitoff, size_t bitmax) override;

    // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails if
    // *bitmax* < *bitoff* or if an allocation is needed and *free_list*
    // does not contain one.
    //
    // *free_list* is a list of usable allocations.  If an allocation is needed,
    // it will be drawn from it.  This function is guaranteed to need at most
    // one allocation.  If any nodes need to be deleted, they will be appended
    // to *free_list*.
    mx_status_t SetNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list);

    // Clears all bits in the range [*bitoff*, *bitmax*).  Only fails on allocation
    // error or if bitmax < bitoff.
    mx_status_t Clear(size_t bitoff, size_t bitmax) override;

    // Clear all bits in the range [*bitoff*, *bitmax*).  Only fails if
    // *bitmax* < *bitoff* or if an allocation is needed and *free_list*
    // does not contain one.
    //
    // *free_list* is a list of usable allocations.  If an allocation is needed,
    // it will be drawn from it.  This function is guaranteed to need at most
    // one allocation.  If any nodes need to be deleted, they will be appended
    // to *free_list*.
    mx_status_t ClearNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list);

    // Clear all bits in the bitmap.
    void ClearAll() override;

    // Iterate over the ranges in the bitmap.  Modifying the list while
    // iterating over it may yield undefined results.
    const_iterator cbegin() const { return elems_.cbegin(); }
    const_iterator begin() const { return elems_.cbegin(); }
    const_iterator end() const { return elems_.cend(); }
    const_iterator cend() const { return elems_.cend(); }

private:
    mx_status_t SetInternal(size_t bitoff, size_t bitmax, FreeList* free_list);
    mx_status_t ClearInternal(size_t bitoff, size_t bitmax, FreeList* free_list);

    // The ranges of the bitmap.
    ListType elems_;

    // The number of ranges in elems_.
    size_t num_elems_;
};

// Elements of the bitmap list
struct RleBitmapElement : public fbl::DoublyLinkedListable<fbl::unique_ptr<RleBitmapElement>> {
    // The start of this run of 1-bits.
    size_t bitoff;
    // The number of 1-bits in this run.
    size_t bitlen;
};

} // namespace bitmap
