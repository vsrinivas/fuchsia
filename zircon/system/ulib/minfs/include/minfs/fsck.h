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

// On success, returns ZX_OK and copies the number of bytes used by data
// within the fs.
zx_status_t UsedDataSize(fbl::unique_ptr<Bcache>& bc, uint64_t* out_size);

// On success, returns ZX_OK and copies the number of allocated
// inodes within the fs.
zx_status_t UsedInodes(fbl::unique_ptr<Bcache>& bc, uint64_t* out_inodes);

// On success, returns ZX_OK and copies the number of bytes used by data
// and bytes reserved for superblock, bitmaps, inodes and journal within the fs.
zx_status_t UsedSize(fbl::unique_ptr<Bcache>& bc, uint64_t* out_size);

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

// Copies into |out_size| the number of bytes used by data in fs contained in a partition between
// bytes |start| and |end| in fd. extent_lengths is lengths of each extent (in bytes).
zx_status_t SparseUsedDataSize(fbl::unique_fd fd, off_t start, off_t end,
                               const fbl::Vector<size_t>& extent_lengths, uint64_t* out_size);
// Copies into |out_inodes| the number of allocated inodes in fs contained in a partition
// between bytes |start| and |end| fd. extent_lengths is lengths of each extent (in bytes).
zx_status_t SparseUsedInodes(fbl::unique_fd fd, off_t start, off_t end,
                             const fbl::Vector<size_t>& extent_lengths, uint64_t* out_inodes);

// Copies into |out_size| the number of bytes used by data and bytes reserved for superblock,
// bitmaps, inodes and journal on fs contained in a partition between bytes |start| and |end| in fd.
// extent_lengths is lengths of each extent (in bytes).
zx_status_t SparseUsedSize(fbl::unique_fd fd, off_t start, off_t end,
                           const fbl::Vector<size_t>& extent_lengths, uint64_t* out_size);

#endif
} // namespace minfs
