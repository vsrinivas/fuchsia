// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes a utility for storing pending allocation state for a Minfs vnode.

#ifndef SRC_STORAGE_MINFS_VNODE_ALLOCATION_H_
#define SRC_STORAGE_MINFS_VNODE_ALLOCATION_H_

#include <bitmap/rle-bitmap.h>
#include <minfs/format.h>

#include "allocator/allocator.h"

namespace minfs {

// PendingAllocationData stores information about data blocks which are yet to be allocated.
// This includes the relative index of each block to be processed, corresponding
// reservations, and (in the future) additional information about modifications to the inode's size
// and block count.
class PendingAllocationData {
 public:
  PendingAllocationData() = default;
  ~PendingAllocationData() { ZX_DEBUG_ASSERT(IsEmpty()); }

  // Clears out all allocation/reservation data.
  void Reset(blk_t size);

  // Returns the |start| and |count| of the first range in the block_map_.
  zx_status_t GetNextRange(blk_t* start, blk_t* count) const;

  // Returns true if no blocks are marked for allocation.
  bool IsEmpty() const { return block_map_.num_bits() == 0 && new_blocks_ == 0; }

  // Returns true if |block_num| is marked in the block_map_.
  bool IsPending(blk_t block_num) const { return block_map_.GetOne(block_num); }

  // Sets |block_num| in the block_map_. |allocated| indicates whether the block at |block_num|
  // was previously allocated.
  void SetPending(blk_t block_num, bool allocated);

  // Clears |block_num| from the block_map_. |allocated| indicates whether the block at
  // |block_num| was previously allocated. Returns true if the block_num was cleared from the map
  // (i.e., it was set in the map initially).
  bool ClearPending(blk_t block_num, bool allocated);

  // Returns the count of pending blocks which are not already allocated.
  blk_t GetNewPending() const { return new_blocks_; }

  // Returns the total number of pending blocks.
  blk_t GetTotalPending() const { return static_cast<blk_t>(block_map_.num_bits()); }

  blk_t GetNodeSize() const { return node_size_; }

  void SetNodeSize(blk_t size) { node_size_ = size; }

 private:
  // Number of additional blocks to be allocated on behalf of the vnode.
  //
  // For example, when writing to a block which has already been allocated to a file,
  // we will allocate a "different" block, but simultenously free the old data block.
  // This results in a net "block change" of zero blocks.
  //
  // This size should always be less than or equal to |block_map_.size()|.
  blk_t new_blocks_ = 0;

  // The expected size of the vnode after all blocks in block_map_ have been allocated.
  blk_t node_size_ = 0;

  // Map of relative data blocks to be allocated at a later time.
  bitmap::RleBitmap block_map_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_VNODE_ALLOCATION_H_
