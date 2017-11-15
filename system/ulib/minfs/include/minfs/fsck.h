// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for checking the consistency
// of a MinFS filesystem.

#pragma once

#include <inttypes.h>

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include <fs/trace.h>

#include <minfs/format.h>
#include <minfs/minfs.h>
#include <minfs/writeback.h>

namespace minfs {

class MinfsChecker {
public:
    MinfsChecker();
    zx_status_t Init(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info);
    zx_status_t CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot);
    zx_status_t CheckForUnusedBlocks() const;
    zx_status_t CheckForUnusedInodes() const;
    zx_status_t CheckLinkCounts() const;
    zx_status_t CheckAllocatedCounts() const;

    // "Set once"-style flag to identify if anything nonconforming
    // was found in the underlying filesystem -- even if it was fixed.
    bool conforming_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MinfsChecker);

    zx_status_t GetInode(minfs_inode_t* inode, ino_t ino);

    // Returns the nth block within an inode, relative to the start of the
    // file. Returns the "next_n" which might contain a bno. This "next_n"
    // is for performance reasons -- it allows fsck to avoid repeatedly checking
    // the same indirect / doubly indirect blocks with all internal
    // bno unallocated.
    zx_status_t GetInodeNthBno(minfs_inode_t* inode, blk_t n, blk_t* next_n,
                               blk_t* bno_out);
    zx_status_t CheckDirectory(minfs_inode_t* inode, ino_t ino,
                               ino_t parent, uint32_t flags);
    const char* CheckDataBlock(blk_t bno);
    zx_status_t CheckFile(minfs_inode_t* inode, ino_t ino);

    fbl::unique_ptr<Minfs> fs_;
    RawBitmap checked_inodes_;
    RawBitmap checked_blocks_;

    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
    fbl::Array<int32_t> links_;

    blk_t cached_doubly_indirect_;
    blk_t cached_indirect_;
    uint8_t doubly_indirect_cache_[kMinfsBlockSize];
    uint8_t indirect_cache_[kMinfsBlockSize];
};

zx_status_t minfs_check_info(const minfs_info_t* info, Bcache* bc);
zx_status_t minfs_check(fbl::unique_ptr<Bcache> bc);

#ifndef __Fuchsia__
// Run fsck on a sparse minfs partition
// |start| indicates where the minfs partition starts within the file (in bytes)
// |end| indicates the end of the minfs partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each minfs extent: currently this includes
// the superblock, inode bitmap, block bitmap, inode table, and data blocks.
zx_status_t minfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                       const fbl::Vector<size_t>& extent_lengths);
#endif
} // namespace minfs
