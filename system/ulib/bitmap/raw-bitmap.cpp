// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/raw-bitmap.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <magenta/types.h>
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
uint64_t GetMask(bool first, bool last, uint64_t off,
                 uint64_t max) {
    uint64_t mask = ~0ULL;
    if (first) {
        mask &= ~0ULL << (off % 64);
    }
    if (last) {
        mask &= ~0ULL >> ((64 - (max % 64)) % 64);
    }
    return mask;
}

} // namespace

namespace bitmap {

RawBitmap::RawBitmap(size_t size)
    : size_(size), bits_(nullptr) {
    if (size != 0) {
        size_t num_idxs = ((size - 1) / 64) + 1;
        bits_.reset(new uint64_t[num_idxs]);
        ClearAll();
    }
}

static_assert(UINT64_MAX == ULLONG_MAX, "uint64_t is not unsigned long long");
bool RawBitmap::Get(uint64_t bitoff, uint64_t bitmax, uint64_t* first) const {
    if (bitoff >= bitmax || bitmax > size_) {
        return true;
    }
    uint64_t i = 0;
    uint64_t first_idx = bitoff / 64;
    uint64_t last_idx = (bitmax - 1) / 64;
    uint64_t value = 0;
    if (!first) {
        first = &value;
    }
    for (i = first_idx; i <= last_idx; ++i) {
        value = ~GetMask(i == first_idx, i == last_idx, bitoff, bitmax);
        value |= bits_[i];
        value = ~value;
        if (value != 0) {
            break;
        }
    }
    *first = (value == 0 ? bitmax : (i * 64) + __builtin_ctzll(value));
    return *first == bitmax;
}

mx_status_t RawBitmap::Set(uint64_t bitoff, uint64_t bitmax) {
    if (bitoff > bitmax || bitmax > size_) {
        return ERR_INVALID_ARGS;
    }
    if (bitoff == bitmax) {
        return NO_ERROR;
    }
    uint64_t first_idx = bitoff / 64;
    uint64_t last_idx = (bitmax - 1) / 64;
    for (uint64_t i = first_idx; i <= last_idx; ++i) {
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
    uint64_t first_idx = bitoff / 64;
    uint64_t last_idx = (bitmax - 1) / 64;
    for (uint64_t i = first_idx; i <= last_idx; ++i) {
        bits_[i] &= ~(GetMask(i == first_idx, i == last_idx, bitoff, bitmax));
    }
    return NO_ERROR;
}

void RawBitmap::ClearAll() {
    if (size_ == 0) {
        return;
    }
    size_t num_idxs = ((size_ - 1) / 64) + 1;
    for (size_t i = 0; i < num_idxs; ++i) {
        bits_[i] = 0;
    }
}

} // namespace bitmap
