// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "block.h"
#include "limits.h"

#include <lib/fzl/resizeable-vmo-mapper.h>
#include <zircon/assert.h>

namespace inspect {
namespace internal {

// A buddy-allocated heap of blocks stored in an extendable VMO.
//
// |inspect::internal::Heap| supports Allocate and Free operations to
// allocate memory stored in a VMO. The VMO may be extended up to a
// maximum size to accommodate allocations.
//
// This class is not thread safe.
class Heap {
public:
    // Create a new heap that allocates out of the given |vmo|.
    // The VMO will grow to accomodate allocations up to a maximum of |max_size|,
    // which must be a multiple of |MIN_VMO_SIZE|.
    Heap(fbl::unique_ptr<fzl::ResizeableVmoMapper> vmo, size_t max_size = kDefaultMaxSize);
    ~Heap();

    zx::vmo ReadOnlyClone() const;

    // Allocate a |BlockIndex| out of the heap that can contain at least |min_size| bytes.
    // Allocating a block larger that |kMaxOrderSize| bytes will fail.
    //
    // Returns ZX_OK on success or an error on failure.
    // |out_block| will be set to the allocated block index on success only.
    //
    // Warning: It is an error to destroy the heap without freeing all blocks first.
    zx_status_t Allocate(size_t min_size, BlockIndex* out_block);

    // Free a |BlockIndex| allocated from this heap.
    void Free(BlockIndex block_index);

    // Get a pointer to the |Block| for the given |Block|.
    Block* GetBlock(BlockIndex block) const {
        return reinterpret_cast<Block*>(((uint8_t*)vmo_->start()) + block * kMinOrderSize);
    }

    // Return a pointer to the data buffer.
    const uint8_t* data() const { return reinterpret_cast<uint8_t*>(vmo_->start()); }

    // Return the current usable size of the VMO.
    size_t size() const { return cur_size_; }

private:
    static constexpr const size_t kDefaultMaxSize = 256 * 1024;

    // Returns true if the given block is free and of the expected order.
    inline bool IsFreeBlock(BlockIndex block, size_t expected_order) const;

    bool SplitBlock(BlockIndex block);
    bool RemoveFree(BlockIndex block);
    zx_status_t Extend(size_t new_size);
    fbl::unique_ptr<fzl::ResizeableVmoMapper> vmo_;
    size_t cur_size_;
    const size_t max_size_;
    BlockIndex free_blocks_[8] = {};

    // Keep track of the number of allocated blocks to assert that they are all freed
    // before the heap is destroyed.
    size_t num_allocated_blocks_ = 0;
};

bool Heap::IsFreeBlock(BlockIndex block, size_t expected_order) const {
    ZX_DEBUG_ASSERT_MSG(block < IndexForOffset(cur_size_), "Block out of bounds");
    if (block >= cur_size_ / kMinOrderSize) {
        return false;
    }
    auto* b = GetBlock(block);
    return GetType(b) == BlockType::kFree && GetOrder(b) == expected_order;
}

} // namespace internal
} // namespace inspect
