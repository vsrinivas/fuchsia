// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes a utility for storing pending allocation state for a Minfs vnode.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <bitmap/rle-bitmap.h>
#include <minfs/format.h>

namespace minfs {

// PendingAllocationData stores information about data blocks which are yet to be allocated.
// This includes the relative index of each block to be processed, corresponding
// reservations, and (in the future) additional information about modifications to the inode's size
// and block count.
class PendingAllocationData {
public:
    PendingAllocationData() = default;

    // Returns the |start| and |count| of the first range in the block_map_.
    zx_status_t GetNextRange(blk_t* start, blk_t* count) const;

    // Returns the size of the longest range in block_map_;
    blk_t GetLongestRange() const;

    // Returns true if no blocks are marked for allocation.
    bool IsEmpty() const { return block_map_.num_bits() == 0; }

    // Returns true if |bno| is marked in the block_map_.
    bool IsPending(blk_t block_num) const { return block_map_.GetOne(block_num); }

    // Sets |bno| in the block_map_.
    // Returns true if the bno was set in the map (i.e., it was not set in the map initially).
    bool SetPending(blk_t block_num);

    // Clears |bno| from the block_map_.
    // Returns true if the bno was cleared from the map (i.e., it was set in the map initially).
    bool ClearPending(blk_t block_num);

    // Returns the total number of pending blocks.
    blk_t GetTotalPending() const { return static_cast<blk_t>(block_map_.num_bits()); }

private:
    // Map of relative data blocks to be allocated at a later time.
    bitmap::RleBitmap block_map_;
};

} // namespace minfs