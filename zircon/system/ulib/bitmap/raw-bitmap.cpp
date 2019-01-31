// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>

#include <limits.h>
#include <stddef.h>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <zircon/types.h>

namespace bitmap {
namespace {

// Translates a bit offset into a starting index in the bitmap array.
constexpr size_t FirstIdx(size_t bitoff) {
    return bitoff / kBits;
}

// GetMask returns a 64-bit bitmask. If the block of the bitmap we're looking
// at isn't the first or last, all bits are set.  Otherwise, the bits outside of
// [off,max) are cleared.  Bits are counted with the LSB as 0 and the MSB as 63.
//
// Examples:
//  GetMask(false, false, 16, 48) => 0xffffffffffffffff
//  GetMask(true,  false, 16, 48) => 0xffffffffffff0000
//  GetMask(false,  true, 16, 48) => 0x0000ffffffffffff
//  GetMask(true,   true, 16, 48) => 0x0000ffffffff0000
size_t GetMask(bool first, bool last, size_t off, size_t max) {
    size_t ones = ~(size_t(0));
    size_t mask = ones;
    if (first) {
        mask &= ones << (off % kBits);
    }
    if (last) {
        mask &= ones >> ((kBits - (max % kBits)) % kBits);
    }
    return mask;
}

// Applies a bitmask to the given data value. The result value has bits set
// which fall within the mask but do not match is_set.
size_t MaskBits(size_t data, size_t idx, size_t bitoff, size_t bitmax,
                bool is_set) {
    size_t mask = GetMask(idx == FirstIdx(bitoff), idx == LastIdx(bitmax),
                          bitoff, bitmax);
    if (is_set) {
        // If is_set=true, invert the mask, OR it with the value, and invert
        // it again to hopefully get all zeros.
        return ~(~mask | data);
    } else {
        // If is_set=false, just AND the mask with the value to hopefully
        // get all zeros.
        return mask & data;
    }
}

// Counts the number of zeros.  It assumes everything in the array up to
// bits_[idx] is zero.
#if (SIZE_MAX == UINT_MAX)
#define CLZ(x) (x == 0 ? kBits : __builtin_clz(x))
#define CTZ(x) (x == 0 ? kBits : __builtin_ctz(x))
#elif (SIZE_MAX == ULONG_MAX)
#define CLZ(x) (x == 0 ? kBits : __builtin_clzl(x))
#define CTZ(x) (x == 0 ? kBits : __builtin_ctzl(x))
#elif (SIZE_MAX == ULLONG_MAX)
#define CLZ(x) (x == 0 ? kBits : __builtin_clzll(x))
#define CTZ(x) (x == 0 ? kBits : __builtin_ctzll(x))
#else
#error "Unsupported size_t length"
#endif

} // namespace

zx_status_t RawBitmapBase::Shrink(size_t size) {
    if (size > size_) {
        return ZX_ERR_NO_MEMORY;
    }
    size_ = size;
    return ZX_OK;
}

bool RawBitmapBase::Scan(size_t bitoff, size_t bitmax, bool is_set,
                         size_t* out) const {
    bitmax = fbl::min(bitmax, size_);
    if (bitoff >= bitmax) {
        return true;
    }
    size_t i = FirstIdx(bitoff);
    while (true) {
        size_t masked = MaskBits(data_[i], i, bitoff, bitmax, is_set);
        if (masked != 0) {
            if (out) {
                *out = i * bitmap::kBits + CTZ(masked);
            }
            return false;
        }
        if (i == LastIdx(bitmax)) {
            return true;
        }
        ++i;
    }
}

bool RawBitmapBase::ReverseScan(size_t bitoff, size_t bitmax, bool is_set,
                          size_t* out) const {
    bitmax = fbl::min(bitmax, size_);
    if (bitoff >= bitmax) {
        return true;
    }
    size_t i = LastIdx(bitmax);
    while (true) {
        size_t masked = MaskBits(data_[i], i, bitoff, bitmax, is_set);
        if (masked != 0) {
            if (out) {
                *out = (i + 1) * bitmap::kBits - (CLZ(masked) + 1);
            }
            return false;
        }
        if (i == FirstIdx(bitoff)) {
            return true;
        }
        --i;
    }
}

zx_status_t RawBitmapBase::Find(bool is_set, size_t bitoff, size_t bitmax,
                                size_t run_len, size_t* out) const {
    if (!out || bitmax <= bitoff) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t start = bitoff;
    while (true) {
        if (Scan(bitoff, bitmax, !is_set, &start) ||
            (bitmax - start < run_len)) {
            return ZX_ERR_NO_RESOURCES;
        }
        if (Scan(start, start + run_len, is_set, &bitoff)) {
            *out = start;
            return ZX_OK;
        }
    }
}

zx_status_t RawBitmapBase::ReverseFind(bool is_set, size_t bitoff, size_t bitmax,
                                 size_t run_len, size_t* out) const {
    if (!out || bitmax <= bitoff) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t start = bitmax;
    while (true) {
        if (ReverseScan(bitoff, bitmax, !is_set, &start)) {
            return ZX_ERR_NO_RESOURCES;
        }
        // Increment start to be last bit that is !is_set
        ++start;
        if ((start - bitoff < run_len)) {
            return ZX_ERR_NO_RESOURCES;
        }
        if (ReverseScan(start - run_len, start, is_set, &bitmax)) {
            *out = start - run_len;
            return ZX_OK;
        }
    }
}

bool RawBitmapBase::Get(size_t bitoff, size_t bitmax, size_t* first) const {
    bool result;
    if ((result = Scan(bitoff, bitmax, true, first)) && first) {
        *first = bitmax;
    }
    return result;
}

zx_status_t RawBitmapBase::Set(size_t bitoff, size_t bitmax) {
    if (bitoff > bitmax || bitmax > size_) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (bitoff == bitmax) {
        return ZX_OK;
    }
    size_t first_idx = FirstIdx(bitoff);
    size_t last_idx = LastIdx(bitmax);
    for (size_t i = first_idx; i <= last_idx; ++i) {
        data_[i] |= GetMask(i == first_idx, i == last_idx, bitoff, bitmax);
    }
    return ZX_OK;
}

zx_status_t RawBitmapBase::Clear(size_t bitoff, size_t bitmax) {
    if (bitoff > bitmax || bitmax > size_) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (bitoff == bitmax) {
        return ZX_OK;
    }
    size_t first_idx = FirstIdx(bitoff);
    size_t last_idx = LastIdx(bitmax);
    for (size_t i = first_idx; i <= last_idx; ++i) {
        data_[i] &= ~(GetMask(i == first_idx, i == last_idx, bitoff, bitmax));
    }
    return ZX_OK;
}

void RawBitmapBase::ClearAll() {
    if (size_ == 0) {
        return;
    }
    size_t last_idx = LastIdx(size_);
    for (size_t i = 0; i <= last_idx; ++i) {
        data_[i] = 0;
    }
}

} // namespace bitmap
