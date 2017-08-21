// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <err.h>
#include <kernel/mutex.h>
#include <list.h>
#include <magenta/types.h>
#include <sys/types.h>

__BEGIN_CDECLS

/**
 * The pow2_range_allocator is a small utility library which partitions a set of
 * ranges of integers into sub-ranges which are power of 2 in length and power
 * of 2 aligned and then manages allocating and freeing the subranges for
 * clients.  It is responsible for breaking larger sub-regions into smaller ones
 * as needed for allocation, and for merging sub-regions into larger sub-regions
 * as needed during free operations.
 *
 * Its primary use is as a utility library for plaforms who need to manage
 * allocating blocks MSI IRQ IDs on behalf of the PCI bus driver, but could (in
 * theory) be used for other things).
 */

typedef struct p2ra_state {
    mutex_t           lock;
    struct list_node  ranges;
    struct list_node  unused_blocks;
    struct list_node  allocated_blocks;
    struct list_node* free_block_buckets;
    uint              bucket_count;
} p2ra_state_t;

/**
 * Initialize the state of a pow2 range allocator.
 *
 * @param state A pointer to the state structure to be initialized.
 * @param max_alloc_size The maximum size of a single contiguous allocation.
 * Must be a power of 2.
 *
 * @return A status code indicating the success or failure of the operation.
 */
mx_status_t p2ra_init(p2ra_state_t* state, uint max_alloc_size);

/**
 * Free all of the state associated with a previously initialized pow2 range
 * allocator.
 *
 * @param state A pointer to the state structure to be cleaned up.
 */
void p2ra_free(p2ra_state_t* state);

/**
 * Add a range of uints to the pool of ranges to be allocated.
 *
 * @param state A pointer to the state structure to add the range to.
 * @param range_start The start of the uint range.
 * @param range_len The length of the uint range.
 *
 * @return A status code incidcating the success or failure of the operation.
 * Possible return values include
 * ++ MX_ERR_INVALID_ARGS range_len is zero, or would cause the range to wrap the
 *    maximum range of a uint.
 * ++ MX_ERR_ALREADY_EXISTS the specified range overlaps with a range already added
 *    to the allocator.
 * ++ MX_ERR_NO_MEMORY Not enough memory to allocate the bookkeeping required for
 *    managing the range.
 */
mx_status_t p2ra_add_range(p2ra_state_t* state, uint range_start, uint range_len);

/**
 * Attempt to allocate a range of uints from the available sub-ranges.  The
 * sizeo the allocated range must be a power of 2, and if the allocation
 * succeeds, it is guaranteed to be aligned on a power of 2 boundary matching it
 * size.
 *
 * @param state A pointer to the state structure to allocate from.
 * @param size The requested size of the region.
 * @param out_range_start An out parameter which will hold the start of the
 * allocated range upon success.
 *
 * @return A status code indicating the success or failure of the operation.
 * Possible return values include
 * ++ MX_ERR_INVALID_ARGS Multiple reasons, including...
 *    ++ size is zero.
 *    ++ size is not a power of two.
 *    ++ out_range_start is NULL.
 * ++ MX_ERR_NO_RESOURCES No contiguous, aligned region could be found to satisfy
 *    the allocation request.
 * ++ MX_ERR_NO_MEMORY A region could be found, but memory required for bookkeeping
 *    could not be allocated.
 */
mx_status_t p2ra_allocate_range(p2ra_state_t* state, uint size, uint* out_range_start);

/**
 * Free a range previously allocated using p2ra_allocate_range.
 *
 * @param state A pointer to the state structure to return the range to.
 * @param range_start The start of the previously allocated range.
 * @param size The size of the previously allocated range.
 */
void p2ra_free_range(p2ra_state_t* state, uint range_start, uint size);

__END_CDECLS
