// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/bitmap.h>

#include <stddef.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/macros.h>
#include <mxtl/unique_ptr.h>

namespace bitmap {

// A simple bitmap backed by an array.
class RawBitmap final : public Bitmap {
public:
    RawBitmap(size_t size);
    virtual ~RawBitmap() = default;
    RawBitmap(RawBitmap&& rhs) = default;
    RawBitmap& operator=(RawBitmap&& rhs) = default;
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(RawBitmap);

    // Resets the bitmap; clearing and resizing it.
    void Reset(size_t size);

    // Returns true if all the bits in [*bitoff*, *bitmax*) are set. Afterwards,
    // *first_unset* will be set to the lesser of bitmax and the index of the
    // first unset bit after *bitoff*.
    bool Get(uint64_t bitoff, uint64_t bitmax,
             uint64_t* first_unset = nullptr) const override;

    // Sets all bits in the range [*bitoff*, *bitmax*).  Returns an error if 
    // bitmax < bitoff or size_ < bitmax, and NO_ERROR otherwise.
    mx_status_t Set(uint64_t bitoff, uint64_t bitmax) override;

    // Clears all bits in the range [*bitoff*, *bitmax*).  Returns an error if 
    // bitmax < bitoff or size_ < bitmax, and NO_ERROR otherwise.
    mx_status_t Clear(uint64_t bitoff, uint64_t bitmax) override;

    // Clear all bits in the bitmap.
    void ClearAll() override;

private:
    // The size of this bitmap, in bits.
    size_t size_;

    // The array backing this bitmap.
    mxtl::unique_ptr<uint64_t[]> bits_;
};

} // namespace bitmap
