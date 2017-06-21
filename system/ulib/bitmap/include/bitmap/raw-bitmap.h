// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/bitmap.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/macros.h>

namespace bitmap {

const size_t kBits = sizeof(size_t) * CHAR_BIT;

// Translates a max bit into a final index in the bitmap array.
constexpr size_t LastIdx(size_t bitmax) {
    return (bitmax - 1) / kBits;
}

// Base class for RawGenericBitmap, to reduce what needs to be templated.
class RawBitmapBase : public Bitmap {
public:
    // Returns the size of this bitmap.
    size_t size() const { return size_; }

    // Shrinks the accessible portion of the bitmap, without re-allocating
    // the underlying storage.
    //
    // This is useful for programs which require underlying bitmap storage
    // to be aligned to a certain size (initialized via Reset), but want to
    // restrict access to a smaller portion of the bitmap (via Shrink).
    mx_status_t Shrink(size_t size);

    // Returns the lesser of bitmax and the index of the first bit that doesn't
    // match *is_set* starting from *bitoff*.
    size_t Scan(size_t bitoff, size_t bitmax, bool is_set) const;

    // Find a run of *run_len* *is_set* bits, between bitoff and bitmax.
    // Returns the start of the run in *out*, or bitmax if it is
    // not found in the provided range.
    // If the run is not found, "MX_ERR_NO_RESOURCES" is returned.
    mx_status_t Find(bool is_set, size_t bitoff, size_t bitmax, size_t run_len, size_t* out) const;

    // Returns true if all the bits in [*bitoff*, *bitmax*) are set. Afterwards,
    // *first_unset* will be set to the lesser of bitmax and the index of the
    // first unset bit after *bitoff*.
    bool Get(size_t bitoff, size_t bitmax,
             size_t* first_unset = nullptr) const override;

    // Sets all bits in the range [*bitoff*, *bitmax*).  Returns an error if
    // bitmax < bitoff or size_ < bitmax, and MX_OK otherwise.
    mx_status_t Set(size_t bitoff, size_t bitmax) override;

    // Clears all bits in the range [*bitoff*, *bitmax*).  Returns an error if
    // bitmax < bitoff or size_ < bitmax, and MX_OK otherwise.
    mx_status_t Clear(size_t bitoff, size_t bitmax) override;

    // Clear all bits in the bitmap.
    void ClearAll() override;

protected:
    // The size of this bitmap, in bits.
    size_t size_ = 0;
    // Owned by bits_, cached
    size_t* data_ = nullptr;
};

// A simple bitmap backed by generic storage.
// Storage must implement:
//   - mx_status_t Allocate(size_t size)
//      To allocate |size| bytes of storage.
//   - void* GetData()
//      To access the underlying storage.
template <typename Storage>
class RawBitmapGeneric final : public RawBitmapBase {
public:
    RawBitmapGeneric() = default;
    virtual ~RawBitmapGeneric() = default;
    RawBitmapGeneric(RawBitmapGeneric&& rhs) = default;
    RawBitmapGeneric& operator=(RawBitmapGeneric&& rhs) = default;
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(RawBitmapGeneric);

    // Resets the bitmap; clearing and resizing it.
    // Allocates memory, and can fail.
    mx_status_t Reset(size_t size) {
        size_ = size;
        if (size_ == 0) {
            data_ = nullptr;
            return MX_OK;
        }
        size_t last_idx = LastIdx(size);
        mx_status_t status = bits_.Allocate(sizeof(size_t) * (last_idx + 1));
        if (status != MX_OK) {
            return status;
        }
        data_ = static_cast<size_t*>(bits_.GetData());
        ClearAll();
        return MX_OK;
    }

    // This function allows access to underlying data, but is dangerous: It
    // leaks the pointer to bits_. Reset and the bitmap destructor should not
    // be called on the bitmap while the pointer returned from data() is alive.
    const Storage* StorageUnsafe() const { return &bits_; }

private:
    // The storage backing this bitmap.
    Storage bits_;
};

} // namespace bitmap
