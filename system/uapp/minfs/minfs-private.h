// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "vfs.h"
#include "minfs.h"

#define MINFS_HASH_BITS (8)
#define MINFS_BUCKETS (1 << MINFS_HASH_BITS)

typedef struct minfs minfs_t;

struct minfs {
    bitmap_t block_map;
    bitmap_t inode_map;
    bcache_t* bc;
    uint32_t abmblks;
    uint32_t ibmblks;
    minfs_info_t info;
    list_node_t vnode_hash[MINFS_BUCKETS];
};

struct vnode {
    // ops, flags, refcount
    VNODE_BASE_FIELDS

    minfs_t* fs;

    uint32_t ino;
    uint32_t reserved;

    list_node_t hashnode;

    minfs_inode_t inode;
};

extern vnode_ops_t minfs_ops;

#define INO_HASH(ino) fnv1a_tiny(ino, MINFS_HASH_BITS)

// instantiate a vnode from an inode
// the inode must exist in the file system
mx_status_t minfs_vnode_get(minfs_t* fs, vnode_t** out, uint32_t ino);

// instantiate a vnode with a new inode
mx_status_t minfs_vnode_new(minfs_t* fs, vnode_t** out, uint32_t type);

// allocate a new data block and bcache_get_zero() it
block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata);

// free ino in inode bitmap
mx_status_t minfs_ino_free(minfs_t* fs, uint32_t ino);

// write the inode data of this vnode to disk
void minfs_sync_vnode(vnode_t* vn);

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max);
void minfs_dump_info(minfs_info_t* info);

mx_status_t minfs_create(minfs_t** out, bcache_t* bc, minfs_info_t* info);
mx_status_t minfs_load_bitmaps(minfs_t* fs);
void minfs_destroy(minfs_t* fs);

int minfs_mkfs(bcache_t* bc);

mx_status_t minfs_check(bcache_t* bc);

mx_status_t minfs_mount(vnode_t** root_out, bcache_t* bc);

mx_status_t minfs_get_vnode(minfs_t* fs, vnode_t** out, uint32_t ino);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);

// get pointer to nth block worth of data in a bitmap
static inline void* minfs_bitmap_nth_block(bitmap_t* bm, uint32_t n) {
    return bitmap_data(bm) + (MINFS_BLOCK_SIZE * n);
}

// get pointer to block of data containing bitno
static inline void* minfs_bitmap_block(bitmap_t* bm, uint32_t* blkno, uint32_t bitno) {
    if (bitno >= bm->bitcount) {
        *blkno = 0;
        return NULL;
    } else {
        uint32_t n = (bitno / MINFS_BLOCK_BITS);
        *blkno = n;
        return bitmap_data(bm) + (MINFS_BLOCK_SIZE * n);
    }
}
