// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// clang-format off

#define MINFS_MAGIC0         (0x002153466e694d21ULL)
#define MINFS_MAGIC1         (0x385000d3d3d3d304ULL)
#define MINFS_VERSION        0x00000001

#define MINFS_ROOT_INO       1
#define MINFS_FLAG_CLEAN     1
#define MINFS_BLOCK_SIZE     8192
#define MINFS_BLOCK_BITS     (MINFS_BLOCK_SIZE * 8)
#define MINFS_INODE_SIZE     256
#define MINFS_INODES_PER_BLOCK (MINFS_BLOCK_SIZE / MINFS_INODE_SIZE)

#define MINFS_DIRECT         16
#define MINFS_INDIRECT       32

#define MINFS_TYPE_FILE      8
#define MINFS_TYPE_DIR       4

#define MINFS_MAGIC(T)       (0xAA6f6e00 | (T))
#define MINFS_MAGIC_DIR      MINFS_MAGIC(MINFS_TYPE_DIR)
#define MINFS_MAGIC_FILE     MINFS_MAGIC(MINFS_TYPE_FILE)
#define MINFS_MAGIC_TYPE(n)  ((n) & 0xFF)

typedef struct {
    uint64_t magic0;
    uint64_t magic1;
    uint32_t version;
    uint32_t flags;
    uint32_t block_size;    // 8K typical
    uint32_t inode_size;    // 256
    uint32_t block_count;   // total number of blocks
    uint32_t inode_count;   // total number of inodes
    uint32_t ibm_block;     // first blockno of inode allocation bitmap
    uint32_t abm_block;     // first blockno of block allocation bitmap
    uint32_t ino_block;     // first blockno of inode table
    uint32_t dat_block;     // first blockno available for file data
} minfs_info_t;

// Notes:
// - the ibm, abm, ino, and dat regions must be in that order
//   and may not overlap
// - the abm has an entry for every block on the volume, including
//   the info block (0), the bitmaps, etc
// - data blocks referenced from direct and indirect block tables
//   in inodes are also relative to (0), but it is not legal for
//   a block number of less than dat_block (start of data blocks)
//   to be used
// - inode numbers refer to the inode in block:
//     ino_block + ino / MINFS_INODES_PER_BLOCK
//   at offset: ino % MINFS_INODES_PER_BLOCK
// - inode 0 is never used, should be marked allocated but ignored

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t block_count;
    uint32_t link_count;
    uint64_t create_time;
    uint64_t modify_time;
    uint32_t seq_num;               // bumped when modified
    uint32_t gen_num;               // bumped when deleted
    uint32_t dirent_count;           // for directories
    uint32_t rsvd[5];
    uint32_t dnum[MINFS_DIRECT];    // direct blocks
    uint32_t inum[MINFS_INDIRECT];  // indirect blocks
} minfs_inode_t;

static_assert(sizeof(minfs_inode_t) == MINFS_INODE_SIZE,
              "minfs inode size is wrong");

typedef struct {
    uint32_t ino;                   // inode number
    uint16_t reclen;                // length of this record
    uint8_t namelen;                // length of the filename
    uint8_t type;                   // MINFS_TYPE_*
    char name[];                    // name does not have trailing \0
} minfs_dirent_t;

#define MINFS_DIRENT_SIZE sizeof(minfs_dirent_t)

#define SIZEOF_MINFS_DIRENT(namelen) (MINFS_DIRENT_SIZE + ((namelen + 3) & (~3)))

// Notes:
// - directory files grow a block at a time
//   and their size is always a multiple of
//   of the fs block size
// - dirents with ino of 0 are freespace, and
//   skipped over on lookup
// - reclen must be a multiple of 4


// blocksize   8K    16K    32K
// 16 dir =  128K   256K   512K
// 32 ind =  512M  1024M  2048M

//  1GB ->  128K blocks ->  16K bitmap (2K qword)
//  4GB ->  512K blocks ->  64K bitmap (8K qword)
// 32GB -> 4096K blocks -> 512K bitmap (64K qwords)



// Allocation Bitmap (bitmap.c)

typedef struct bitmap bitmap_t;
struct bitmap {
    uint32_t bitcount;
    uint32_t mapcount;
    uint64_t *map;
    uint64_t *end;
};


/* orr
static inline void* bitmap_data(bitmap_t* bm) {
    return bm->map;
}

#define BITMAP_FAIL (0xFFFFFFFF)

// find an available bit, set it, return that bitnumber
// returns BITMAP_FAIL if no bit is found
uint32_t bitmap_alloc(bitmap_t* bm, uint32_t minbit);
*/


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
int bcache_close(bcache_t* bc);

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

mx_status_t bcache_sync(bcache_t* bc);

uint32_t bcache_max_block(bcache_t* bc);

// drop all non-busy, non-dirty blocks
void bcache_invalidate(bcache_t* bc);
