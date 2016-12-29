// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/vfs.h>

#include "minfs.h"

Bitmap::Bitmap() {}
Bitmap::~Bitmap() {}

// Number of 64-bit wide 'maps'.
uint32_t Bitmap::Mapcount() const {
    return (bitcount_ + 63) / 64;
}

// Get a pointer to the final 64-bit 'map'.
uint64_t* Bitmap::End() const {
    return map_.get() + Mapcount();
}

size_t Bitmap::BytesRequired() const {
    // round the allocated buffer size to a fs block size to ensure
    // that we don't partially write a block out
    size_t size = sizeof(uint64_t) * Mapcount();
    return ((size + kMinfsBlockSize - 1) / kMinfsBlockSize) * kMinfsBlockSize;
}

mx_status_t Bitmap::Init(uint32_t max) {
    // check for overflow
    if ((max + 63) < max) {
        return ERR_INVALID_ARGS;
    }

    // ensure there's space for all the bits
    bitcount_ = max;

    map_.reset(static_cast<uint64_t*>(calloc(1, BytesRequired())));
    if (map_ == nullptr) {
        return ERR_NO_MEMORY;
    }
    return NO_ERROR;
}

void Bitmap::Reset() {
    memset(map_.get(), 0, BytesRequired());
}

mx_status_t Bitmap::Resize(uint32_t max) {
    if (max > bitcount_) {
        return ERR_NO_MEMORY;
    }
    bitcount_ = max;
    return NO_ERROR;
}

void Bitmap::Set(uint32_t n) {
    assert(n < bitcount_);
    // Shifting by 6 because 1 << 6 == 64
    map_.get()[n >> 6] |= (1ULL << (n & 63));
}

void Bitmap::Clr(uint32_t n) {
    assert(n < bitcount_);
    map_.get()[n >> 6] &= ~((1ULL << (n & 63)));
}

bool Bitmap::Get(uint32_t n) const {
    assert(n < bitcount_);
    return (map_.get()[n >> 6] & (1ULL << (n & 63))) != 0;
}

// minbit specifies a bit number which is the minimum to allocate at
// to avoid making all allocations suffer, we round to the nearest
// multiple of the sub-bitmap storage unit (a uint64_t).
uint32_t Bitmap::Alloc(uint32_t minbit) {
    uint64_t* bits = map_.get() + ((minbit + 63) >> 6);
    while (bits < End()) {
        uint64_t v = *bits;
        uint32_t n;
        if ((n = __builtin_ffsll(~v)) != 0) {
            n--;
            uint32_t ret = (uint32_t) (((bits - map_.get()) << 6) + n);
            // If this is end-1, and the map is full
            // we might attempt to use a bit past the
            // end.  Ensure that we do not.
            if (ret >= bitcount_) {
                break;
            }
            *bits = v | (1ULL << n);
            return ret;
        }
        bits++;
    }
    return BITMAP_FAIL;
}

#define FAIL_IF(c) do { if (c) { error("fail: %s\n", #c); return -1; } } while (0)

int do_bitmap_test(void) {
    Bitmap bm;
    if (bm.Init(1024)) {
        error("init failed\n");
        return -1;
    }
    uint32_t n;

    bm.Set(1);
    bm.Set(64);
    bm.Set(65);
    bm.Set(64 + 8);
    bm.Alloc(63);
    uint64_t *map = (uint64_t*) bm.data();
    FAIL_IF(map[0] != 2);
    FAIL_IF(map[1] != 0x107);

    bm.Reset();
    for (n = 128; n < 1024; n++) {
        FAIL_IF(bm.Alloc(128) != n);
    }
    FAIL_IF(bm.Alloc(128) != BITMAP_FAIL);
    for (n = 64; n < 128; n++) {
        FAIL_IF(bm.Alloc(19) != n);
    }
    for (n = 0; n < 64; n++) {
        FAIL_IF(bm.Alloc(0) != n);
    }
    FAIL_IF(bm.Alloc(0) != BITMAP_FAIL);

    bm.Clr(793);
    FAIL_IF(bm.Alloc(0) != 793);

    for (n = 1023; n > 32; n -= 17) {
        bm.Clr(n);
        FAIL_IF(bm.Alloc(0) != n);
    }

    bm.Reset();
    map = (uint64_t*) bm.data();
    for (n = 0; n < 10; n++) {
        map[n] = -1;
    }
    FAIL_IF(bm.Alloc(0) != 640);

    memset(map, 0xFF, bm.Capacity() / 8);
    FAIL_IF(bm.Alloc(0) != BITMAP_FAIL);

    warn("bitmap: ok\n");
    return 0;
}

