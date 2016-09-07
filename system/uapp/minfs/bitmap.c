// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vfs.h"
#include "minfs.h"

mx_status_t bitmap_init(bitmap_t* bm, uint32_t max) {
    // check for overflow
    if ((max + 63) < max) {
        return ERR_INVALID_ARGS;
    }

    // ensure there's space for all the bits
    bm->bitcount = max;
    bm->mapcount = (max + 63) / 64;

    // round the allocated buffer size to a fs block size to ensure
    // that we don't partially write a block out
    size_t size = 64 * bm->mapcount;
    size = ((size + MINFS_BLOCK_SIZE - 1) / MINFS_BLOCK_SIZE) * MINFS_BLOCK_SIZE;

    if ((bm->map = calloc(1, size)) == NULL) {
        return ERR_NO_MEMORY;
    }
    bm->end = bm->map + bm->mapcount;
    return NO_ERROR;
}

mx_status_t bitmap_resize(bitmap_t* bm, uint32_t max) {
    if (max > (bm->mapcount * 64)) {
        return ERR_NO_MEMORY;
    }
    bm->bitcount = max;
    return NO_ERROR;
}

void bitmap_destroy(bitmap_t* bm) {
    free(bm->map);
}

static void bitmap_zero(bitmap_t* bm) {
    memset(bm->map, 0, bm->bitcount / 8);
}

// minbit specifies a bit number which is the minimum to allocate at
// to avoid making all allocations suffer, we round to the nearest
// multiple of the sub-bitmap storage unit (a uint64_t).
uint32_t bitmap_alloc(bitmap_t* bm, uint32_t minbit) {
    uint64_t* bits = bm->map + ((minbit + 63) >> 6);
    uint64_t* end = bm->end;
    while (bits < end) {
        uint64_t v = *bits;
        uint32_t n;
        if ((n = __builtin_ffsll(~v)) != 0) {
            n--;
            uint32_t ret = ((bits - bm->map) << 6) + n;
            // If this is end-1, and the map is full
            // we might attempt to use a bit past the
            // end.  Ensure that we do not.
            if (ret >= bm->bitcount) {
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
    bitmap_t bm;
    if (bitmap_init(&bm, 1024)) {
        error("init failed\n");
        return -1;
    }
    uint32_t n;

    bitmap_set(&bm, 1);
    bitmap_set(&bm, 64);
    bitmap_set(&bm, 65);
    bitmap_set(&bm, 64 + 8);
    bitmap_alloc(&bm, 63);
    FAIL_IF(bm.map[0] != 2);
    FAIL_IF(bm.map[1] != 0x107);

    bitmap_zero(&bm);
    for (n = 128; n < 1024; n++) {
        FAIL_IF(bitmap_alloc(&bm, 128) != n);
    }
    FAIL_IF(bitmap_alloc(&bm, 128) != BITMAP_FAIL);
    for (n = 64; n < 128; n++) {
        FAIL_IF(bitmap_alloc(&bm, 19) != n);
    }
    for (n = 0; n < 64; n++) {
        FAIL_IF(bitmap_alloc(&bm, 0) != n);
    }
    FAIL_IF(bitmap_alloc(&bm, 0) != BITMAP_FAIL);

    bitmap_clr(&bm, 793);
    FAIL_IF(bitmap_alloc(&bm, 0) != 793);

    for (n = 1023; n > 32; n -= 17) {
        bitmap_clr(&bm, n);
        FAIL_IF(bitmap_alloc(&bm, 0) != n);
    }

    bitmap_zero(&bm);
    for (n = 0; n < 10; n++) {
        bm.map[n] = -1;
    }
    FAIL_IF(bitmap_alloc(&bm, 0) != 640);

    memset(bm.map, 0xFF, bm.bitcount / 8);
    FAIL_IF(bitmap_alloc(&bm, 0) != BITMAP_FAIL);

    warn("bitmap: ok\n");
    return 0;
}

