// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for checking the consistency
// of a MinFS filesystem.

#ifndef SRC_STORAGE_MINFS_FSCK_H_
#define SRC_STORAGE_MINFS_FSCK_H_

#include <inttypes.h>

#include <memory>

#include <fbl/array.h>
#include <fbl/vector.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"

#ifdef __Fuchsia__
#include <block-client/cpp/block-device.h>
#endif

namespace minfs {

enum class BlockType {
  kDirect = 0,     // Direct block contains user data.
  kIndirect,       // Contains an array of block numbers pointing to direct blocks.
  kDoubleIndirect  // Contains an array of block numbers of pointing to indirect blocks.
};

#ifdef __Fuchsia__
// Validates header information.
zx_status_t CheckSuperblock(const Superblock* info, block_client::BlockDevice* device,
                            uint32_t max_blocks);
// Reconstructs and updates the alloc_*_counts of superblock.
zx_status_t ReconstructAllocCounts(fs::TransactionHandler* transaction_handler,
                                   block_client::BlockDevice* device, Superblock* out_info);
#else
// Validates header information.
zx_status_t CheckSuperblock(const Superblock* info, uint32_t max_blocks);
// Reconstructs and updates the alloc_*_counts of superblock.
zx_status_t ReconstructAllocCounts(fs::TransactionHandler* transaction_handler,
                                   Superblock* out_info);
#endif

struct FsckOptions {
  // If true, try and repair the file-system if necessary.
  bool repair = false;

  // If true, treat the volume as read-only and refrain from sending any writes to the volume,
  // including marking the volume as cleanly unmounted.
  bool read_only = false;

  // If true, be sparing with messages.
  bool quiet = false;
};

// Updates generation_count and checksum of the superblock.
void UpdateChecksum(Superblock* info);

// Loads superblock from disk and checks it for integrity.
zx_status_t LoadSuperblock(Bcache* bc, Superblock* out);

// Repair corrupted superblock from backup.
#ifdef __Fuchsia__
zx_status_t RepairSuperblock(fs::DeviceTransactionHandler* transaction_handler,
                             block_client::BlockDevice* device, uint32_t max_blocks,
                             Superblock* info_out);
#endif

// On success, returns ZX_OK and copies the number of bytes used by data
// within the fs.
zx_status_t UsedDataSize(std::unique_ptr<Bcache>& bc, uint64_t* out_size);

// On success, returns ZX_OK and copies the number of allocated
// inodes within the fs.
zx_status_t UsedInodes(std::unique_ptr<Bcache>& bc, uint64_t* out_inodes);

// On success, returns ZX_OK and copies the number of bytes used by data
// and bytes reserved for superblock, bitmaps, inodes and journal within the fs.
zx_status_t UsedSize(std::unique_ptr<Bcache>& bc, uint64_t* out_size);

// Run fsck on an unmounted filesystem backed by |bc|.
//
// Invokes CheckSuperblock, and repairs filesystem if needed.
// On success, returns bcache to |out_bc|, if supplied.
zx_status_t Fsck(std::unique_ptr<Bcache> bc, const FsckOptions& options,
                 std::unique_ptr<Bcache>* out_bc = nullptr);

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
}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_FSCK_H_
