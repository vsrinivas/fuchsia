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

#include <fbl/auto_call.h>
#include <kernel/lockdep.h>

#define LOCAL_TRACE 0

Pow2RangeAllocator::Block* Pow2RangeAllocator::GetUnusedBlock() {
  if (!list_is_empty(&unused_blocks_))
    return list_remove_head_type(&unused_blocks_, Block, node);

  fbl::AllocChecker ac;
  Block* block = new (&ac) Block{};
  if (!ac.check()) {
    return nullptr;
  } else {
    return block;
  }
}

void Pow2RangeAllocator::FreeBlockList(struct list_node* block_list) {
  Block* block;
  while ((block = list_remove_head_type(block_list, Block, node)) != nullptr)
    delete block;
}

void Pow2RangeAllocator::FreeRangeList(struct list_node* range_list) {
  Range* range;
  while ((range = list_remove_head_type(range_list, Range, node)) != nullptr)
    delete range;
}

void Pow2RangeAllocator::ReturnFreeBlock(Block* block, bool merge_allowed) {
  DEBUG_ASSERT(block);
  DEBUG_ASSERT(block->bucket < bucket_count_);
  DEBUG_ASSERT(!list_in_list(&block->node));
  DEBUG_ASSERT(!(block->start & ((1u << block->bucket) - 1)));

  // Return the block to its proper free bucket, sorted by base ID.  Start by
  // finding the block which should come after this block in the list.
  struct list_node* l = &free_block_buckets_[block->bucket];
  Block* after = list_peek_head_type(l, Block, node);
  uint32_t block_len = 1u << block->bucket;

  while (after) {
    // We do not allow ranges to overlap.
    __UNUSED uint32_t after_len = 1u << after->bucket;
    DEBUG_ASSERT((block->start >= (after->start + after_len)) ||
                 (after->start >= (block->start + block_len)));

    if (after->start > block->start) {
      list_add_before(&after->node, &block->node);
      break;
    }

    // Advance the iterator.
    after = list_next_type(l, &after->node, Block, node);
  }

  // If no block comes after this one, it goes on the end of the list.
  if (!after)
    list_add_tail(l, &block->node);

  // Don't merge blocks in the largest bucket.
  if (block->bucket + 1 == bucket_count_)
    return;

  // Check to see if we should be merging this block into a larger aligned block.
  Block* first;
  Block* second;
  if (block->start & ((block_len << 1) - 1)) {
    // Odd alignment.  This might be the second block of a merge pair.
    second = block;
    first = list_prev_type(l, &block->node, Block, node);
  } else {
    // Even alignment.  This might be the first block of a merge pair.
    first = block;
    second = list_next_type(l, &block->node, Block, node);
  }

  // Do these chunks fit together?
  if (first && second) {
    uint32_t first_len = 1u << first->bucket;
    if ((first->start + first_len) == second->start) {
      // Assert that we are allowed to perform a merge.  If the caller is
      // not expecting us to have to merge anything, then there is a fatal
      // bookkeeping error somewhere
      DEBUG_ASSERT(merge_allowed);
      DEBUG_ASSERT(first->bucket == second->bucket);

      // Remove the two blocks' bookkeeping from their bucket.
      list_delete(&first->node);
      list_delete(&second->node);

      // Place one half of the bookkeeping back on the unused list.
      list_add_tail(&unused_blocks_, &second->node);

      // Reuse the other half to track the newly merged block, and place
      // it in the next bucket size up.
      first->bucket++;
      ReturnFreeBlock(first, merge_allowed);
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
  free_block_buckets_ = new (&ac) list_node[bucket_count_];
  if (!ac.check()) {
    TRACEF("Failed to allocate storage for %u free bucket lists!\n", bucket_count_);
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize the rest of our bookeeping.
  list_initialize(&ranges_);
  list_initialize(&unused_blocks_);
  list_initialize(&allocated_blocks_);
  for (uint32_t i = 0; i < bucket_count_; ++i)
    list_initialize(&free_block_buckets_[i]);

  return ZX_OK;
}

void Pow2RangeAllocator::Free() {
  DEBUG_ASSERT(bucket_count_);
  DEBUG_ASSERT(free_block_buckets_);
  DEBUG_ASSERT(list_is_empty(&allocated_blocks_));

  FreeRangeList(&ranges_);
  FreeBlockList(&unused_blocks_);
  FreeBlockList(&allocated_blocks_);
  for (uint32_t i = 0; i < bucket_count_; ++i)
    FreeBlockList(&free_block_buckets_[i]);
}

zx_status_t Pow2RangeAllocator::AddRange(uint32_t range_start, uint32_t range_len) {
  LTRACEF("Adding range [%u, %u]\n", range_start, range_start + range_len - 1);

  if (!range_len || ((range_start + range_len) < range_start))
    return ZX_ERR_INVALID_ARGS;

  zx_status_t ret = ZX_OK;
  Range* new_range = nullptr;
  struct list_node new_blocks;
  list_initialize(&new_blocks);

  // If we're exiting with a failure, clean up anything we've allocated.
  auto auto_call = fbl::MakeAutoCall([&]() {
    if (ret != ZX_OK) {
      if (new_range) {
        DEBUG_ASSERT(!list_in_list(&new_range->node));
        delete new_range;
      }

      FreeBlockList(&new_blocks);
    }
  });

  // Enter the lock and check for overlap with pre-existing ranges.
  Guard<Mutex> guard{&lock_};

  Range* range;
  list_for_every_entry (&ranges_, range, Range, node) {
    if (((range->start >= range_start) && (range->start < (range_start + range_len))) ||
        ((range_start >= range->start) && (range_start < (range->start + range->len)))) {
      TRACEF("Range [%u, %u] overlaps with existing range [%u, %u].\n", range_start,
             range_start + range_len - 1, range->start, range->start + range->len - 1);
      ret = ZX_ERR_ALREADY_EXISTS;
      return ret;
    }
  }

  // Allocate our range state.
  fbl::AllocChecker ac;
  new_range = new (&ac) Range{};
  if (!ac.check()) {
    ret = ZX_ERR_NO_MEMORY;
    return ret;
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

    Block* block = GetUnusedBlock();
    if (!block) {
      TRACEF(
          "WARNING! Failed to allocate block bookkeeping with sub-range "
          "[%u, %u] still left to track.\n",
          range_start, range_start + range_len - 1);
      ret = ZX_ERR_NO_MEMORY;
      return ret;
    }

    block->bucket = bucket;
    block->start = range_start;
    list_add_tail(&new_blocks, &block->node);

    range_start += csize;
    range_len -= csize;
  }

  // Looks like we managed to allocate everything we needed to.  Go ahead and
  // add all of our newly allocated bookkeeping to the state.
  list_add_tail(&ranges_, &new_range->node);

  Block* block;
  while ((block = list_remove_head_type(&new_blocks, Block, node)) != nullptr)
    ReturnFreeBlock(block, true);

  return ret;
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
  Block* block = nullptr;

  Guard<Mutex> guard{&lock_};

  // Find the smallest sized chunk which can hold the allocation and is
  // compatible with the requested addressing capabilities.
  while (bucket < bucket_count_) {
    block = list_remove_head_type(&free_block_buckets_[bucket], Block, node);
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
    Block* split_block = GetUnusedBlock();

    // If we failed to allocate bookkeeping for the split block, put the block
    // we failed to split back into the free list (merging if required),
    // then fail the allocation.
    if (!split_block) {
      TRACEF(
          "Failed to allocated free bookkeeping block when attempting to "
          "split for allocation\n");
      ReturnFreeBlock(block, true);
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
    ReturnFreeBlock(split_block, false);
  }

  // Success! Mark the block as allocated and return the block to the user.
  list_add_head(&allocated_blocks_, &block->node);
  *out_range_start = block->start;

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
  Block* block;
#if DEBUG_ASSERT_IMPLEMENTED
  block = list_peek_head_type(&allocated_blocks_, Block, node);
  while (block) {
    if ((block->start == range_start) && (block->bucket == bucket)) {
      list_delete(&block->node);
      break;
    }
    block = list_next_type(&allocated_blocks_, &block->node, Block, node);
  }
  ASSERT(block);
#else
  block = list_remove_head_type(&allocated_blocks_, Block, node);
  ASSERT(block);
  block->start = range_start;
  block->bucket = bucket;
#endif

  // Return the block to the free buckets (merging as needed) and we are done.
  ReturnFreeBlock(block, true);
}
