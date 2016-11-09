// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/macros.h>

namespace bitmap {

// An abstract bitmap..
class Bitmap {
public:
    virtual ~Bitmap() = default;

    // Returns true in the bit at bitoff is set.
    virtual bool GetOne(uint64_t bitoff) const {
        return Get(bitoff, bitoff + 1, nullptr);
    }

    // Returns true if all the bits in [*bitoff*, *bitmax*) are set. Afterwards,
    // *first_unset* will be set to the lesser of bitmax and the index of the
    // first unset bit after *bitoff*.
    virtual bool Get(uint64_t bitoff, uint64_t bitmax,
                     uint64_t* first_unset = nullptr) const = 0;

    // Sets the bit at bitoff.  Only fails on allocation error.
    virtual mx_status_t SetOne(uint64_t bitoff) {
        return Set(bitoff, bitoff + 1);
    }

    // Sets all bits in the range [*bitoff*, *bitmax*).  Only fails on
    // allocation error or if bitmax < bitoff.
    virtual mx_status_t Set(uint64_t bitoff, uint64_t bitmax) = 0;

    // Clears the bit at bitoff.  Only fails on allocation error.
    virtual mx_status_t ClearOne(uint64_t bitoff) {
        return Clear(bitoff, bitoff + 1);
    }

    // Clears all bits in the range [*bitoff*, *bitmax*).  Only fails on
    // allocation error or if bitmax < bitoff.
    virtual mx_status_t Clear(uint64_t bitoff, uint64_t bitmax) = 0;

    // Clear all bits in the bitmap.
    virtual void ClearAll() = 0;
};

} // namespace bitmap
