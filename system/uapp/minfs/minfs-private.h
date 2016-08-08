// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "vfs.h"
#include "minfs.h"

#define MINFS_HASH_BITS (8)
#define MINFS_BUCKETS (1 << MINFS_HASH_BITS)

typedef struct minfs minfs_t;
typedef struct minfs_vnode minfs_vnode_t;

extern vnode_ops_t minfs_ops;

struct minfs {
    bitmap_t block_map;
    bitmap_t inode_map;
    bcache_t* bc;
    uint32_t abmblks;
    uint32_t ibmblks;
    minfs_info_t info;
    vfs_t vfs;
    list_node_t vnode_hash[MINFS_BUCKETS];
};

struct minfs_vnode {
    list_node_t hashnode;
    minfs_t* fs;
    uint32_t ino;

    vnode_t vnode;
    minfs_inode_t inode;
};

#define INO_HASH(ino) fnv_1a_tiny(ino, MINFS_HASH_BITS)

// instantiate a vnode from an inode
// the inode must exist in the file system
mx_status_t minfs_get_vnode(minfs_t* fs, minfs_vnode_t** out, uint32_t ino);

// instantiate a vnode with a new inode
mx_status_t minfs_new_vnode(minfs_t* fs, minfs_vnode_t** out, uint32_t type);

// delete the inode backing a vnode
mx_status_t minfs_del_vnode(minfs_vnode_t* vn);

// allocate a new data block and bcache_get_zero() it
block_t* minfs_new_block(minfs_t* fs, uint32_t hint, uint32_t* out_bno, void** bdata);

// write the inode data of this vnode to disk
void minfs_sync_vnode(minfs_vnode_t* vn);

#define to_minfs(_vfs) (containerof(_vfs, minfs_t, vfs))
#define to_minvn(_vn) (containerof(_vn, minfs_vnode_t, vnode))

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max);
void minfs_dump_info(minfs_info_t* info);

mx_status_t minfs_create(minfs_t** out, bcache_t* bc, minfs_info_t* info);
mx_status_t minfs_load_bitmaps(minfs_t* fs);
void minfs_destroy(minfs_t* fs);

int minfs_mkfs(bcache_t* bc);

mx_status_t minfs_check(bcache_t* bc);

mx_status_t minfs_mount(vnode_t** root_out, bcache_t* bc);

mx_status_t minfs_get_vnode(minfs_t* fs, minfs_vnode_t** out, uint32_t ino);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);