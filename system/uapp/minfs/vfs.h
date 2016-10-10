// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>
#include <mxio/vfs.h>

#include "misc.h"

// VFS Helpers (vfs.c)

mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout);

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out,
                     const char* path, uint32_t flags, uint32_t mode);

mx_status_t vfs_find(vnode_t* vndir, vnode_t** out,
                     const char* path, const char** path_out);

mx_status_t vfs_close(vnode_t* vn);

mx_status_t vfs_rename(vnode_t* vndir, const char* oldpath, const char* newpath);

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type);

// VFS RPC Server (rpc.c)

mx_handle_t vfs_rpc_server(vnode_t* vn, const char* where);


// Allocation Bitmap (bitmap.c)

typedef struct bitmap bitmap_t;
struct bitmap {
    uint32_t bitcount;
    uint32_t mapcount;
    uint64_t *map;
    uint64_t *end;
};

mx_status_t bitmap_init(bitmap_t* bm, uint32_t maxbits);
void bitmap_destroy(bitmap_t* bm);

// This will never fail if the new maxbits is no larger
// that the original maxbits.  The underlying storage will
// not be reduced (so this is useful for creating a bitmap
// to match a particular storage size and then adjust it
// to a maximum allowed bit smaller than the storage)
mx_status_t bitmap_resize(bitmap_t* bm, uint32_t maxbits);

static inline void bitmap_set(bitmap_t* bm, uint32_t n) {
    if (n < bm->bitcount) {
        bm->map[n >> 6] |= (1ULL << (n & 63));
    }
}

static inline void bitmap_clr(bitmap_t* bm, uint32_t n) {
    if (n < bm->bitcount) {
        bm->map[n >> 6] &= ~((1ULL << (n & 63)));
    }
}

static inline bool bitmap_get(bitmap_t* bm, uint32_t n) {
    if (n < bm->bitcount) {
        return (bm->map[n >> 6] & (1ULL << (n & 63))) != 0;
    } else {
        return 0;
    }
}

static inline void* bitmap_data(bitmap_t* bm) {
    return bm->map;
}

#define BITMAP_FAIL (0xFFFFFFFF)

// find an available bit, set it, return that bitnumber
// returns BITMAP_FAIL if no bit is found
uint32_t bitmap_alloc(bitmap_t* bm, uint32_t minbit);


// Block Cache (bcache.c)

typedef struct bcache bcache_t;
typedef struct block block_t;

int bcache_create(bcache_t** out, int fd, uint32_t blockmax, uint32_t blocksize, uint32_t num);

#define BLOCK_DIRTY 1

// acquire a block, reading from disk if necessary,
// returning a handle and a pointer to the data
block_t* bcache_get(bcache_t* bc, uint32_t bno, void** bdata);

// acquire a block, not reading from disk, marking dirty,
// and clearing to all 0s
block_t* bcache_get_zero(bcache_t* bc, uint32_t bno, void** block);

// release a block back to the cache
// flags *must* contain BLOCK_DIRTY if it was modified
void bcache_put(bcache_t* bc, block_t* blk, uint32_t flags);

mx_status_t bcache_read(bcache_t* bc, uint32_t bno, void* data, uint32_t off, uint32_t len);

uint32_t bcache_max_block(bcache_t* bc);

// drop all non-busy, non-dirty blocks
void bcache_invalidate(bcache_t* bc);

// General Utilities

#define panic(fmt...) do { fprintf(stderr, fmt); *((int*) 0) = 0; } while (0)
#define error(fmt...) fprintf(stderr, fmt)
#define warn(fmt...) fprintf(stderr, fmt)
#define info(fmt...) fprintf(stderr, fmt)

#define TRACE_MINFS   0x0001
#define TRACE_VFS     0x0010
#define TRACE_WALK    0x0020
#define TRACE_REFS    0x0040
#define TRACE_BCACHE  0x0100
#define TRACE_IO      0x0200
#define TRACE_RPC     0x0400
#define TRACE_VERBOSE 0x1000

#define TRACE_SOME    0x0001
#define TRACE_ALL     0xFFFF

// Enable trace printf()s

extern uint32_t __trace_bits;

static inline void trace_on(uint32_t bits) {
    __trace_bits |= bits;
}

static inline void trace_off(uint32_t bits) {
    __trace_bits &= (~bits);
}

#define trace(what,fmt...) do { if (__trace_bits & (TRACE_##what)) fprintf(stderr, fmt); } while (0)
