// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/raw-bitmap.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <magenta/assert.h>
#include <magenta/types.h>
#include <mxtl/algorithm.h>
#include <mxtl/macros.h>
#include <mxtl/unique_ptr.h>

namespace {

// GetMask returns a 64-bit bitmask.  If the block of the bitmap we're looking
// at isn't the first or last, all bits are set.  Otherwise, the bits outside of
// [off,max) are cleared.  Bits are counted with the LSB as 0 and the MSB as 63.
//
// Examples:
//  GetMask(false, false, 16, 48) => 0xffffffffffffffff
//  GetMask(true,  false, 16, 48) => 0xffffffffffff0000
//  GetMask(false,  true, 16, 48) => 0x0000ffffffffffff
//  GetMask(true,   true, 16, 48) => 0x0000ffffffff0000
uint64_t GetMask(bool first, bool last, uint64_t off, uint64_t max) {
    uint64_t mask = ~0ULL;
    if (first) {
        mask &= ~0ULL << (off % 64);
    }
    if (last) {
        mask &= ~0ULL >> ((64 - (max % 64)) % 64);
    }
    return mask;
}

// Translates a bit offset into a starting index in the bitmap array.
size_t FirstIdx(uint64_t bitoff) {
    uint64_t idx = bitoff / 64;
    DEBUG_ASSERT(idx < SIZE_MAX);
    return static_cast<size_t>(idx);
}

// Translates a max bit into a final index in the bitmap array.
size_t LastIdx(uint64_t bitmax) {
    uint64_t idx = (bitmax - 1) / 64;
    DEBUG_ASSERT(idx < SIZE_MAX);
    return static_cast<size_t>(idx);
}

// Counts the number of zeros.  It assumes everything in the array up to
// bits_[idx] is zero.
static_assert(UINT64_MAX == ULLONG_MAX, "uint64_t is not unsigned long long");
uint64_t CountZeros(size_t idx, uint64_t value) {
    return idx * 64 + (value == 0 ? 64 : __builtin_ctzll(value));
}

} // namespace

namespace bitmap {

RawBitmap::RawBitmap(size_t size)
    : size_(0), bits_(nullptr) {
    Reset(size);
}

// Resets the bitmap; clearing and resizing it.
void RawBitmap::Reset(size_t size) {
    size_ = size;
    if (size_ == 0) {
        bits_.reset(nullptr);
        return;
    }
    size_t last_idx = LastIdx(size);
    bits_.reset(new uint64_t[last_idx + 1]);
    ClearAll();
}

bool RawBitmap::Get(uint64_t bitoff, uint64_t bitmax, uint64_t* first) const {
    if (bitoff >= bitmax || bitmax > size_) {
        return true;
    }
    size_t first_idx = FirstIdx(bitoff);
    size_t last_idx = LastIdx(bitmax);
    size_t i = first_idx;
    uint64_t value = 0;
    if (!first) {
        first = &value;
    }
    for (; i <= last_idx; ++i) {
        value = ~GetMask(i == first_idx, i == last_idx, bitoff, bitmax);
        value |= bits_[i];
        value = ~value;
        if (value != 0) {
            break;
        }
    }
    *first = mxtl::min(bitmax, CountZeros(i, value));
    return *first == bitmax;
}

mx_status_t RawBitmap::Set(uint64_t bitoff, uint64_t bitmax) {
    if (bitoff > bitmax || bitmax > size_) {
        return ERR_INVALID_ARGS;
    }
    if (bitoff == bitmax) {
        return NO_ERROR;
    }
    size_t first_idx = FirstIdx(bitoff);
    size_t last_idx = LastIdx(bitmax);
    for (size_t i = first_idx; i <= last_idx; ++i) {
        bits_[i] |= GetMask(i == first_idx, i == last_idx, bitoff, bitmax);
    }
    return NO_ERROR;
}

mx_status_t RawBitmap::Clear(uint64_t bitoff, uint64_t bitmax) {
    if (bitoff > bitmax || bitmax > size_) {
        return ERR_INVALID_ARGS;
    }
    if (bitoff == bitmax) {
        return NO_ERROR;
    }
    size_t first_idx = FirstIdx(bitoff);
    size_t last_idx = LastIdx(bitmax);
    for (size_t i = first_idx; i <= last_idx; ++i) {
        bits_[i] &= ~(GetMask(i == first_idx, i == last_idx, bitoff, bitmax));
    }
    return NO_ERROR;
}

void RawBitmap::ClearAll() {
    if (size_ == 0) {
        return;
    }
    size_t last_idx = LastIdx(size_);
    for (size_t i = 0; i <= last_idx; ++i) {
        bits_[i] = 0;
    }
}

} // namespace bitmap
