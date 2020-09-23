// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions that are shared between host
// and target implementations of Blobfs.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMMON_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMMON_H_

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <blobfs/format.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/string_buffer.h>
#include <fs/transaction/transaction_handler.h>

namespace blobfs {

// The minimum size of a blob that we will consider for compression. Attempting
// to compress a blob smaller than this will not result in any size savings, so
// we can just skip it and save some work.
constexpr uint64_t kCompressionSizeThresholdBytes = kBlobfsBlockSize;

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

// Validates the metadata of a blobfs superblock, given a disk with |max| blocks.
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max);

// Returns the total number of vslices needed for the given partition.
uint32_t CalculateVsliceCount(const Superblock& info);

// Returns number of blocks required for inode_count inodes
uint32_t BlocksRequiredForInode(uint64_t inode_count);

// Returns number of blocks required for bit_count bits
uint32_t BlocksRequiredForBits(uint64_t bit_count);

// Suggests a journal size, in blocks.
// |current|: The current size of the journal, in blocks.
// |available|: An additional number of blocks available which may be used by the journal.
uint32_t SuggestJournalBlocks(uint32_t current, uint32_t available);

// Creates a superblock, formatted for |block_count| disk blocks, on a non-FVM volume.
// This method should also be invoked to create FVM-based superblocks, but it is the responsibility
// of the caller to update |info->flags| to include |kBlobFlagFVM|, and fill in all
// FVM-specific fields.
void InitializeSuperblock(uint64_t block_count, Superblock* info);

// Computes the number of blocks necessary to store the merkle tree for the blob, based on its size.
// May return 0 for small blobs (for which only the root digest is sufficient to verify the entire
// contents of the blob).
uint32_t ComputeNumMerkleTreeBlocks(const Inode& blobNode);

// Get a pointer to the nth block of the bitmap.
inline void* GetRawBitmapData(const RawBitmap& bm, uint64_t n) {
  assert(n * kBlobfsBlockSize < bm.size());                // Accessing beyond end of bitmap
  assert(kBlobfsBlockSize <= (n + 1) * kBlobfsBlockSize);  // Avoid overflow
  return fs::GetBlock(kBlobfsBlockSize, bm.StorageUnsafe()->GetData(), n);
}

// Fills |out| with the VMO names for a blob with inode index |node_index|.
void FormatBlobDataVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out);
void FormatBlobCompressedVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out);
void FormatBlobMerkleVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_COMMON_H_
