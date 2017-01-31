// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "minfs.h"
#include "misc.h"

#include <bitmap/raw-bitmap.h>
#include <mxtl/algorithm.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

#include <fs/vfs.h>

#define panic(fmt...) do { fprintf(stderr, fmt); __builtin_trap(); } while (0)

// minfs_sync_vnode flags
constexpr uint32_t kMxFsSyncDefault = 0;     // default: no implicit time update
constexpr uint32_t kMxFsSyncMtime   = (1<<0);
constexpr uint32_t kMxFsSyncCtime   = (1<<1);

constexpr uint32_t kMinfsBlockCacheSize = 64;

// Used by fsck
struct CheckMaps {
    bitmap::RawBitmap checked_inodes;
    bitmap::RawBitmap checked_blocks;
};

class Minfs {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Minfs);

    static mx_status_t Create(Minfs** out, Bcache* bc, minfs_info_t* info);

    mx_status_t Unmount();

    // instantiate a vnode from an inode
    // the inode must exist in the file system
    mx_status_t VnodeGet(vnode_t** out, uint32_t ino);

    // instantiate a vnode with a new inode
    mx_status_t VnodeNew(vnode_t** out, uint32_t type);

    // Allocate a new data block and bcache_get_zero() it.
    // Acquires the block if out_block is not null.
    mx_status_t BlockNew(uint32_t hint, uint32_t* out_bno, mxtl::RefPtr<BlockNode>* out_block);

    // free ino in inode bitmap, release all blocks held by inode
    mx_status_t InoFree(const minfs_inode_t& inode, uint32_t ino);

    // When modifying bit 'n' in the bitmap, the following pattern may be used:
    //   blk = nullptr;
    //   blk = BitmapBlockGet(blk, n);
    //   block_map.Access(n);
    //   BitmapBlockPut(blk);
    // To guarantee the in-memory bitmap is in sync with the on-disk bitmap. Repeated
    // access to related (nearby) bits in the bitmap will defer writing to the disk
    // until "BitmapBlockPut" is called.
    mxtl::RefPtr<BlockNode> BitmapBlockGet(const mxtl::RefPtr<BlockNode>& blk, uint32_t n);
    void BitmapBlockPut(const mxtl::RefPtr<BlockNode>& blk);

    Bcache* bc;
    bitmap::RawBitmap block_map;
    minfs_info_t info;
private:
    Minfs(Bcache* bc_, minfs_info_t* info_);

    mx_status_t InoNew(minfs_inode_t* inode, uint32_t* ino_out);
    mx_status_t LoadBitmaps();

    uint32_t abmblks_;
    uint32_t ibmblks_;
    bitmap::RawBitmap inode_map_;
    list_node_t vnode_hash_[kMinfsBuckets];

    // Fsck can introspect Minfs
    friend mx_status_t check_inode(CheckMaps*, const Minfs*, uint32_t, uint32_t);
    friend mx_status_t minfs_check(Bcache*);
};

struct vnode {
    // ops, flags, refcount
    VNODE_BASE_FIELDS

    Minfs* fs;

    uint32_t ino;
    uint32_t reserved;

    list_node_t hashnode;

#ifdef __Fuchsia__
    // TODO(smklein): When we have can register MinFS as a pager service, and
    // it can properly handle pages faults on a vnode's contents, then we can
    // avoid reading the entire file up-front. Until then, read the contents of
    // a VMO into memory when it is read/written.
    mx_handle_t vmo;
#endif

    minfs_inode_t inode;
};

static_assert(mxtl::is_standard_layout<struct vnode>::value, "Vnode must be standard layout to be placed in a list_node_t");

extern vnode_ops_t minfs_ops;

#define INO_HASH(ino) fnv1a_tiny(ino, kMinfsHashBits)

// write the inode data of this vnode to disk (default does not update time values)
void minfs_sync_vnode(vnode_t* vn, uint32_t flags);

mx_status_t minfs_check_info(minfs_info_t* info, uint32_t max);
void minfs_dump_info(minfs_info_t* info);

int minfs_mkfs(Bcache* bc);

mx_status_t minfs_check(Bcache* bc);

mx_status_t minfs_mount(vnode_t** root_out, Bcache* bc);

void minfs_dir_init(void* bdata, uint32_t ino_self, uint32_t ino_parent);

// vfs dispatch
mx_handle_t vfs_rpc_server(vnode_t* vn);
