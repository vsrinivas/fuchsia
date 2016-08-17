// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// clang-format off

#define MINFS_MAGIC0         (0x002153466e694d21ULL)
#define MINFS_MAGIC1         (0x22385077ADACABAAULL)
#define MINFS_VERSION        0x00000001

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
