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
#include <string.h>
#include <stdlib.h>
#include <trace.h>

#define LOCAL_TRACE 0

typedef struct p2ra_block {
    struct list_node node;
    uint             bucket;
    uint             start;
} p2ra_block_t;

typedef struct p2ra_range {
    struct list_node node;
    uint             start, len;
} p2ra_range_t;

static inline p2ra_block_t* p2ra_get_unused_block(p2ra_state_t* state) {
    DEBUG_ASSERT(state);

    if (!list_is_empty(&state->unused_blocks))
        return list_remove_head_type(&state->unused_blocks, p2ra_block_t, node);

    return (p2ra_block_t*)calloc(1, sizeof(p2ra_block_t));
}

static inline void p2ra_free_block_list(struct list_node* block_list) {
    p2ra_block_t* block;
    while ((block = list_remove_head_type(block_list, p2ra_block_t, node)) != NULL)
        free(block);
}

static inline void p2ra_free_range_list(struct list_node* range_list) {
    p2ra_range_t* range;
    while ((range = list_remove_head_type(range_list, p2ra_range_t, node)) != NULL)
        free(range);
}

static void p2ra_return_free_block(p2ra_state_t* state,
                                   p2ra_block_t* block,
                                   bool merge_allowed) {
    DEBUG_ASSERT(state);
    DEBUG_ASSERT(block);
    DEBUG_ASSERT(block->bucket < state->bucket_count);
    DEBUG_ASSERT(!list_in_list(&block->node));
    DEBUG_ASSERT(!(block->start & ((1u << block->bucket) - 1)));

    /* Return the block to its proper free bucket, sorted by base ID.  Start by
     * finding the block which should come after this block in the list. */
    struct list_node* l         = &state->free_block_buckets[block->bucket];
    p2ra_block_t*     after     = list_peek_head_type(l, p2ra_block_t, node);
    uint              block_len = 1u << block->bucket;

    while (after) {
        /* We do not allow ranges to overlap */
        __UNUSED uint after_len = 1u << after->bucket;
        DEBUG_ASSERT((block->start >= (after->start + after_len)) ||
                     (after->start >= (block->start + block_len)));

        if (after->start > block->start) {
            list_add_before(&after->node, &block->node);
            break;
        }

        /* Advance the iterator */
        after = list_next_type(l, &after->node, p2ra_block_t, node);
    }

    /* If no block comes after this one, it goes on the end of the list */
    if (!after)
        list_add_tail(l, &block->node);

    /* Don't merge blocks in the largest bucket. */
    if (block->bucket + 1 == state->bucket_count)
        return;

    /* Check to see if we should be merging this block into a larger aligned block. */
    p2ra_block_t* first;
    p2ra_block_t* second;
    if (block->start & ((block_len << 1) - 1)) {
        /* Odd alignment.  This might be the second block of a merge pair */
        second = block;
        first  = list_prev_type(l, &block->node, p2ra_block_t, node);
    } else {
        /* Even alignment.  This might be the first block of a merge pair */
        first  = block;
        second = list_next_type(l, &block->node, p2ra_block_t, node);
    }

    /* Do these chunks fit together? */
    if (first && second) {
        uint first_len = 1u << first->bucket;
        if ((first->start + first_len) == second->start) {
            /* Assert that we are allowed to perform a merge.  If the caller is
             * not expecting us to have to merge anything, then there is a fatal
             * bookkeeping error somewhere */
            DEBUG_ASSERT(merge_allowed);
            DEBUG_ASSERT(first->bucket == second->bucket);

            /* Remove the two blocks' bookkeeping from their bucket */
            list_delete(&first->node);
            list_delete(&second->node);

            /* Place one half of the bookkeeping back on the unused list */
            list_add_tail(&state->unused_blocks, &second->node);

            /* Reuse the other half to track the newly merged block, and place
             * it in the next bucket size up. */
            first->bucket++;
            p2ra_return_free_block(state, first, merge_allowed);
        }
    }
}


mx_status_t p2ra_init(p2ra_state_t* state, uint max_alloc_size) {
    if (!state)
        return MX_ERR_INVALID_ARGS;

    if (!max_alloc_size || !ispow2(max_alloc_size)) {
        TRACEF("max_alloc_size (%u) is not an integer power of two!\n", max_alloc_size);
        return MX_ERR_INVALID_ARGS;
    }

    /* Allocate the storage for our free buckets */
    state->bucket_count = log2_uint_floor(max_alloc_size) + 1;
    state->free_block_buckets = malloc(state->bucket_count * sizeof(state->free_block_buckets[0]));
    if (!state->free_block_buckets) {
        TRACEF("Failed to allocate storage for %u free bucket lists!\n", state->bucket_count);
        return MX_ERR_NO_MEMORY;
    }

    /* Initialize the rest of our bookeeping */
    mutex_init(&state->lock);
    list_initialize(&state->ranges);
    list_initialize(&state->unused_blocks);
    list_initialize(&state->allocated_blocks);
    for (uint i = 0; i < state->bucket_count; ++i)
        list_initialize(&state->free_block_buckets[i]);

    return MX_OK;
}

void p2ra_free(p2ra_state_t* state) {
    DEBUG_ASSERT(state);
    DEBUG_ASSERT(state->bucket_count);
    DEBUG_ASSERT(state->free_block_buckets);
    DEBUG_ASSERT(list_is_empty(&state->allocated_blocks));

    p2ra_free_range_list(&state->ranges);
    p2ra_free_block_list(&state->unused_blocks);
    p2ra_free_block_list(&state->allocated_blocks);
    for (uint i = 0; i < state->bucket_count; ++i)
        p2ra_free_block_list(&state->free_block_buckets[i]);

    mutex_destroy(&state->lock);
    memset(state, 0, sizeof(*state));
}

mx_status_t p2ra_add_range(p2ra_state_t* state, uint range_start, uint range_len) {
    LTRACEF("Adding range [%u, %u]\n", range_start, range_start + range_len - 1);

    if (!state      ||
        !range_len  ||
        ((range_start + range_len) < range_start))
        return MX_ERR_INVALID_ARGS;

    mx_status_t      ret       = MX_OK;
    p2ra_range_t*    new_range = NULL;
    struct list_node new_blocks;
    list_initialize(&new_blocks);

    /* Enter the lock and check for overlap with pre-existing ranges */
    mutex_acquire(&state->lock);

    p2ra_range_t* range;
    list_for_every_entry(&state->ranges, range, p2ra_range_t, node) {
        if (((range->start >= range_start)  && (range->start < (range_start  + range_len))) ||
            ((range_start  >= range->start) && (range_start  < (range->start + range->len)))) {
            TRACEF("Range [%u, %u] overlaps with existing range [%u, %u].\n",
                    range_start,  range_start  + range_len  - 1,
                    range->start, range->start + range->len - 1);
            ret = MX_ERR_ALREADY_EXISTS;
            goto finished;
        }
    }

    /* Allocate our range state */
    new_range = calloc(1, sizeof(*new_range));
    if (!new_range) {
        ret = MX_ERR_NO_MEMORY;
        goto finished;
    }
    new_range->start = range_start;
    new_range->len   = range_len;

    /* Break the range we were given into power of two aligned chunks, and place
     * them on the new blocks list to be added to the free-blocks buckets */
    DEBUG_ASSERT(state->bucket_count && state->free_block_buckets);
    uint bucket     = state->bucket_count - 1;
    uint csize      = (1u << bucket);
    uint max_csize  = csize;
    while (range_len) {
        /* Shrink the chunk size until it is aligned with the start of the
         * range, and not larger than the number of irqs we have left. */
        bool shrunk = false;
        while ((range_start & (csize - 1)) || (range_len < csize)) {
            csize >>= 1;
            bucket--;
            shrunk = true;
        }

        /* If we didn't need to shrink the chunk size, perhaps we can grow it
         * instead. */
        if (!shrunk) {
            uint tmp = csize << 1;
            while ((tmp <= max_csize) &&
                   (tmp <= range_len) &&
                   (!(range_start & (tmp - 1)))) {
                bucket++;
                csize = tmp;
                tmp <<= 1;
                DEBUG_ASSERT(bucket < state->bucket_count);
            }
        }

        /* Break off a chunk of the range */
        DEBUG_ASSERT((1u << bucket) == csize);
        DEBUG_ASSERT(bucket < state->bucket_count);
        DEBUG_ASSERT(!(range_start & (csize - 1)));
        DEBUG_ASSERT(csize <= range_len);
        DEBUG_ASSERT(csize);

        p2ra_block_t* block = p2ra_get_unused_block(state);
        if (!block) {
            TRACEF("WARNING! Failed to allocate block bookkeeping with sub-range "
                   "[%u, %u] still left to track.\n",
                   range_start, range_start + range_len - 1);
            ret = MX_ERR_NO_MEMORY;
            goto finished;
        }

        block->bucket = bucket;
        block->start  = range_start;
        list_add_tail(&new_blocks, &block->node);

        range_start += csize;
        range_len   -= csize;
    }

    /* Looks like we managed to allocate everything we needed to.  Go ahead and
     * add all of our newly allocated bookkeeping to the state. */
    list_add_tail(&state->ranges, &new_range->node);

    p2ra_block_t* block;
    while ((block = list_remove_head_type(&new_blocks, p2ra_block_t, node)) != NULL)
        p2ra_return_free_block(state, block, false);


finished:
    mutex_release(&state->lock);

    if (ret != MX_OK) {
        if (new_range) {
            DEBUG_ASSERT(!list_in_list(&new_range->node));
            free(new_range);
        }

        p2ra_free_block_list(&new_blocks);
    }

    return ret;
}

mx_status_t p2ra_allocate_range(p2ra_state_t* state, uint size, uint* out_range_start) {
    if (!state || !out_range_start)
        return MX_ERR_INVALID_ARGS;

    if (!size || !ispow2(size)) {
        TRACEF("Size (%u) is not an integer power of 2.\n", size);
        return MX_ERR_INVALID_ARGS;
    }

    uint orig_bucket = log2_uint_floor(size);
    uint bucket      = orig_bucket;
    if (bucket >= state->bucket_count) {
        TRACEF("Invalid size (%u).  Valid sizes are integer powers of 2 from [1, %u]\n",
                size, 1u << (state->bucket_count - 1));
        return MX_ERR_INVALID_ARGS;
    }

    /* Lock state during allocation */
    p2ra_block_t* block = NULL;
    mx_status_t   ret   = MX_OK;
    mutex_acquire(&state->lock);

    /* Find the smallest sized chunk which can hold the allocation and is
     * compatible with the requested addressing capabilities */
    while (bucket < state->bucket_count) {
        block = list_remove_head_type(&state->free_block_buckets[bucket], p2ra_block_t, node);
        if (block)
            break;
        bucket++;
    }

    /* Nothing found, unlock and get out */
    if (!block) {
        ret = MX_ERR_NO_RESOURCES;
        goto finished;
    }

    /* Looks like we have a chunk which can satisfy this allocation request.
     * Split it as many times as needed to match the requested size. */
    DEBUG_ASSERT(block->bucket == bucket);
    DEBUG_ASSERT(bucket >= orig_bucket);

    while (bucket > orig_bucket) {
        p2ra_block_t* split_block = p2ra_get_unused_block(state);

        /* If we failed to allocate bookkeeping for the split block, put the block
         * we failed to split back into the free list (merging if required),
         * then fail the allocation */
        if (!split_block) {
            TRACEF("Failed to allocated free bookkeeping block when attempting to "
                   "split for allocation\n");
            p2ra_return_free_block(state, block, true);
            ret = MX_ERR_NO_MEMORY;
            goto finished;
        }

        DEBUG_ASSERT(bucket);
        bucket--;

        /* Cut the first chunk in half */
        block->bucket = bucket;

        /* Fill out the bookkeeping for the second half of the chunk */
        split_block->start  = block->start + (1u << block->bucket);
        split_block->bucket = bucket;

        /* Return the second half of the chunk to the free pool */
        p2ra_return_free_block(state, split_block, false);
    }

    /* Success! Mark the block as allocated and return the block to the user */
    list_add_head(&state->allocated_blocks, &block->node);
    *out_range_start = block->start;

finished:
    mutex_release(&state->lock);
    return ret;
}

void p2ra_free_range(p2ra_state_t* state, uint range_start, uint size) {
    DEBUG_ASSERT(state);
    DEBUG_ASSERT(size && ispow2(size));

    uint bucket = log2_uint_floor(size);

    mutex_acquire(&state->lock);

    /* In a debug build, find the specific block being returned in the list of
     * allocated blocks and use it as the bookkeeping for returning to the free
     * bucket.  Because this is an O(n) operation, and serves only as a sanity
     * check, we only do this in debug builds.  In release builds, we just grab
     * any piece of bookkeeping memory off the allocated_blocks list and use
     * that instead. */
    p2ra_block_t* block;
#if DEBUG_ASSERT_IMPLEMENTED
    block = list_peek_head_type(&state->allocated_blocks, p2ra_block_t, node);
    while (block) {
        if ((block->start == range_start) && (block->bucket == bucket)) {
            list_delete(&block->node);
            break;
        }
        block = list_next_type(&state->allocated_blocks, &block->node, p2ra_block_t, node);
    }
    ASSERT(block);
#else
    block         = list_remove_head_type(&state->allocated_blocks, p2ra_block_t, node);
    ASSERT(block);
    block->start  = range_start;
    block->bucket = bucket;
#endif

    /* Return the block to the free buckets (merging as needed) and we are done */
    p2ra_return_free_block(state, block, true);
    mutex_release(&state->lock);
}
