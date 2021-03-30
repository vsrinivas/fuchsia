// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <lib/pow2_range_allocator.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <fbl/alloc_checker.h>
#include <kernel/lockdep.h>
#include <ktl/move.h>

#define LOCAL_TRACE 0

ktl::unique_ptr<Pow2RangeAllocator::Block> Pow2RangeAllocator::GetUnusedBlock() {
  if (!unused_blocks_.is_empty())
    return unused_blocks_.pop_front();

  fbl::AllocChecker ac;
  auto block = ktl::make_unique<Block>(&ac);
  if (!ac.check()) {
    return nullptr;
  } else {
    return block;
  }
}

void Pow2RangeAllocator::ReturnFreeBlock(ktl::unique_ptr<Block> block) {
  DEBUG_ASSERT(block);
  DEBUG_ASSERT(block->bucket < bucket_count_);
  DEBUG_ASSERT(!block->InContainer());
  DEBUG_ASSERT(!(block->start & ((1u << block->bucket) - 1)));

  // Return the block to its proper free bucket, sorted by base ID.  Start by
  // finding the block which should come after this block in the list.
  BlockList& list = free_block_buckets_[block->bucket];
  uint32_t block_len = 1u << block->bucket;

  // We'll need this to make an iterator in the list, after we insert |block|.
  Block& raw_block = *block;

  bool inserted = false;
  for (Block& after : list) {
    // We do not allow ranges to overlap.
    __UNUSED uint32_t after_len = 1u << after.bucket;
    DEBUG_ASSERT((block->start >= (after.start + after_len)) ||
                 (after.start >= (block->start + block_len)));

    if (after.start > block->start) {
      list.insert(after, ktl::move(block));
      inserted = true;
      break;
    }
  }

  // If no block comes after this one, it goes on the end of the list.
  if (!inserted)
    list.push_back(ktl::move(block));

  // After this point, we can no longer access |block|.

  // Get an iterator to the block we just pushed.
  auto iter = list.make_iterator(raw_block);

  // Don't merge blocks in the largest bucket.
  if (raw_block.bucket + 1 == bucket_count_)
    return;

  // Check to see if we should be merging this block into a larger aligned block.
  Block* first;
  Block* second;
  if (raw_block.start & ((block_len << 1) - 1)) {
    // Odd alignment.  This might be the second block of a merge pair.
    second = &raw_block;
    if (iter == list.begin()) {
      first = nullptr;
    } else {
      --iter;
      first = &*iter;
    }
  } else {
    // Even alignment.  This might be the first block of a merge pair.
    first = &raw_block;
    ++iter;
    if (iter == list.end()) {
      second = nullptr;
    } else {
      second = &*iter;
    }
  }

  // Do these chunks fit together?
  if (first && second) {
    uint32_t first_len = 1u << first->bucket;
    if ((first->start + first_len) == second->start) {
      DEBUG_ASSERT(first->bucket == second->bucket);

      // Remove the two blocks' bookkeeping from their bucket.
      auto owned_first = list.erase(*first);
      auto owned_second = list.erase(*second);

      // Place one half of the bookkeeping back on the unused list.
      unused_blocks_.push_back(ktl::move(owned_second));

      // Reuse the other half to track the newly merged block, and place
      // it in the next bucket size up.
      owned_first->bucket++;
      ReturnFreeBlock(ktl::move(owned_first));
    }
  }
}

zx_status_t Pow2RangeAllocator::Init(uint32_t max_alloc_size) {
  if (!max_alloc_size || !ispow2(max_alloc_size)) {
    TRACEF("max_alloc_size (%u) is not an integer power of two!\n", max_alloc_size);
    return ZX_ERR_INVALID_ARGS;
  }

  // Allocate the storage for our free buckets.
  bucket_count_ = log2_uint_floor(max_alloc_size) + 1;
  fbl::AllocChecker ac;
  free_block_buckets_ = ktl::make_unique<BlockList[]>(&ac, bucket_count_);
  if (!ac.check()) {
    TRACEF("Failed to allocate storage for %u free bucket lists!\n", bucket_count_);
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

void Pow2RangeAllocator::Free() {
  DEBUG_ASSERT(bucket_count_);
  DEBUG_ASSERT(free_block_buckets_);
  DEBUG_ASSERT(allocated_blocks_.is_empty());

  ranges_.clear();
  unused_blocks_.clear();
  allocated_blocks_.clear();
  for (uint32_t i = 0; i < bucket_count_; ++i)
    free_block_buckets_[i].clear();
}

zx_status_t Pow2RangeAllocator::AddRange(uint32_t range_start, uint32_t range_len) {
  LTRACEF("Adding range [%u, %u]\n", range_start, range_start + range_len - 1);

  if (!range_len || ((range_start + range_len) < range_start))
    return ZX_ERR_INVALID_ARGS;

  ktl::unique_ptr<Range> new_range;
  fbl::DoublyLinkedList<ktl::unique_ptr<Block>> new_blocks;

  // Enter the lock and check for overlap with pre-existing ranges.
  Guard<Mutex> guard{&lock_};

  for (Range& range : ranges_) {
    if (((range.start >= range_start) && (range.start < (range_start + range_len))) ||
        ((range_start >= range.start) && (range_start < (range.start + range.len)))) {
      TRACEF("Range [%u, %u] overlaps with existing range [%u, %u].\n", range_start,
             range_start + range_len - 1, range.start, range.start + range.len - 1);
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  // Allocate our range state.
  fbl::AllocChecker ac;
  new_range = ktl::make_unique<Range>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  new_range->start = range_start;
  new_range->len = range_len;

  // Break the range we were given into power of two aligned chunks, and place
  // them on the new blocks list to be added to the free-blocks buckets.
  DEBUG_ASSERT(bucket_count_ && free_block_buckets_);
  uint32_t bucket = bucket_count_ - 1;
  uint32_t csize = (1u << bucket);
  uint32_t max_csize = csize;
  while (range_len) {
    // Shrink the chunk size until it is aligned with the start of the
    // range, and not larger than the number of irqs we have left.
    bool shrunk = false;
    while ((range_start & (csize - 1)) || (range_len < csize)) {
      csize >>= 1;
      bucket--;
      shrunk = true;
    }

    // If we didn't need to shrink the chunk size, perhaps we can grow it
    // instead.
    if (!shrunk) {
      uint32_t tmp = csize << 1;
      while ((tmp <= max_csize) && (tmp <= range_len) && (!(range_start & (tmp - 1)))) {
        bucket++;
        csize = tmp;
        tmp <<= 1;
        DEBUG_ASSERT(bucket < bucket_count_);
      }
    }

    // Break off a chunk of the range.
    DEBUG_ASSERT((1u << bucket) == csize);
    DEBUG_ASSERT(bucket < bucket_count_);
    DEBUG_ASSERT(!(range_start & (csize - 1)));
    DEBUG_ASSERT(csize <= range_len);
    DEBUG_ASSERT(csize);

    ktl::unique_ptr<Block> block = GetUnusedBlock();
    if (!block) {
      TRACEF(
          "WARNING! Failed to allocate block bookkeeping with sub-range "
          "[%u, %u] still left to track.\n",
          range_start, range_start + range_len - 1);
      return ZX_ERR_NO_MEMORY;
    }

    block->bucket = bucket;
    block->start = range_start;
    new_blocks.push_back(ktl::move(block));

    range_start += csize;
    range_len -= csize;
  }

  // Looks like we managed to allocate everything we needed to.  Go ahead and
  // add all of our newly allocated bookkeeping to the state.
  ranges_.push_back(ktl::move(new_range));

  ktl::unique_ptr<Block> block;
  while ((block = new_blocks.pop_front()) != nullptr)
    ReturnFreeBlock(ktl::move(block));

  return ZX_OK;
}

zx_status_t Pow2RangeAllocator::AllocateRange(uint32_t size, uint* out_range_start) {
  if (!out_range_start)
    return ZX_ERR_INVALID_ARGS;

  if (!size || !ispow2(size)) {
    TRACEF("Size (%u) is not an integer power of 2.\n", size);
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t orig_bucket = log2_uint_floor(size);
  uint32_t bucket = orig_bucket;
  if (bucket >= bucket_count_) {
    TRACEF("Invalid size (%u).  Valid sizes are integer powers of 2 from [1, %u]\n", size,
           1u << (bucket_count_ - 1));
    return ZX_ERR_INVALID_ARGS;
  }

  // Lock state during allocation.
  ktl::unique_ptr<Block> block;

  Guard<Mutex> guard{&lock_};

  // Find the smallest sized chunk which can hold the allocation and is
  // compatible with the requested addressing capabilities.
  while (bucket < bucket_count_) {
    block = free_block_buckets_[bucket].pop_front();
    if (block)
      break;
    bucket++;
  }

  // Nothing found, unlock and get out.
  if (!block) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Looks like we have a chunk which can satisfy this allocation request.
  // Split it as many times as needed to match the requested size.
  DEBUG_ASSERT(block->bucket == bucket);
  DEBUG_ASSERT(bucket >= orig_bucket);

  while (bucket > orig_bucket) {
    ktl::unique_ptr<Block> split_block = GetUnusedBlock();

    // If we failed to allocate bookkeeping for the split block, put the block
    // we failed to split back into the free list (merging if required),
    // then fail the allocation.
    if (!split_block) {
      TRACEF(
          "Failed to allocated free bookkeeping block when attempting to "
          "split for allocation\n");
      ReturnFreeBlock(ktl::move(block));
      return ZX_ERR_NO_MEMORY;
    }

    DEBUG_ASSERT(bucket);
    bucket--;

    // Cut the first chunk in half.
    block->bucket = bucket;

    // Fill out the bookkeeping for the second half of the chunk.
    split_block->start = block->start + (1u << block->bucket);
    split_block->bucket = bucket;

    // Return the second half of the chunk to the free pool.
    ReturnFreeBlock(ktl::move(split_block));
  }

  // Success! Mark the block as allocated and return the block to the user.
  *out_range_start = block->start;
  allocated_blocks_.push_front(ktl::move(block));

  return ZX_OK;
}

void Pow2RangeAllocator::FreeRange(uint32_t range_start, uint32_t size) {
  DEBUG_ASSERT(size && ispow2(size));

  uint32_t bucket = log2_uint_floor(size);

  Guard<Mutex> guard{&lock_};

  // In a debug build, find the specific block being returned in the list of
  // allocated blocks and use it as the bookkeeping for returning to the free
  // bucket.  Because this is an O(n) operation, and serves only as a sanity
  // check, we only do this in debug builds.  In release builds, we just grab
  // any piece of bookkeeping memory off the allocated_blocks list and use
  // that instead.
  ktl::unique_ptr<Block> block;
#if DEBUG_ASSERT_IMPLEMENTED
  for (Block& candidate : allocated_blocks_) {
    if ((candidate.start == range_start) && (candidate.bucket == bucket)) {
      block = allocated_blocks_.erase(candidate);
      break;
    }
  }
  ASSERT(block);
#else
  block = allocated_blocks_.pop_front();
  ASSERT(block);
  block->start = range_start;
  block->bucket = bucket;
#endif

  // Return the block to the free buckets (merging as needed) and we are done.
  ReturnFreeBlock(ktl::move(block));
}
