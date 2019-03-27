// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for checking the consistency
// of a MinFS filesystem.

#pragma once

#include <inttypes.h>

#include <minfs/bcache.h>
#include <minfs/format.h>
#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs/trace.h>

namespace minfs {

// Validate header information about the filesystem backed by |bc|.
zx_status_t CheckSuperblock(const Superblock* info, Bcache* bc);

// Run fsck on an unmounted filesystem backed by |bc|.
//
// Invokes CheckSuperblock, but also verifies inode and block usage.
zx_status_t Fsck(fbl::unique_ptr<Bcache> bc);

// Returns number of blocks required to store inode_count inodes
uint32_t BlocksRequiredForInode(uint64_t inode_count);

// Returns number of blocks required to store bit_count bits
uint32_t BlocksRequiredForBits(uint64_t bit_count);

#ifndef __Fuchsia__
// Run fsck on a sparse minfs partition
// |start| indicates where the minfs partition starts within the file (in bytes)
// |end| indicates the end of the minfs partition (in bytes)
// |extent_lengths| contains the length (in bytes) of each minfs extent: currently this includes
// the superblock, inode bitmap, block bitmap, inode table, and data blocks.
zx_status_t SparseFsck(fbl::unique_fd fd, off_t start, off_t end,
                       const fbl::Vector<size_t>& extent_lengths);
#endif
} // namespace minfs
