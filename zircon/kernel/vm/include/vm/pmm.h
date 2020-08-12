// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <vm/page.h>
#include <vm/page_queues.h>
#include <vm/page_request.h>

// physical allocator
typedef struct pmm_arena_info {
  char name[16];

  uint flags;

  paddr_t base;
  size_t size;
} pmm_arena_info_t;

#define PMM_ARENA_FLAG_LO_MEM \
  (0x1)  // this arena is contained within architecturally-defined 'low memory'

// Add a pre-filled memory arena to the physical allocator.
// The arena data will be copied.
zx_status_t pmm_add_arena(const pmm_arena_info_t* arena) __NONNULL((1));

// Returns the number of arenas.
size_t pmm_num_arenas();

// Copies |count| pmm_arena_info_t objects into |buffer| starting with the |i|-th arena ordered by
// base address.  For example, passing an |i| of 1 would skip the 1st arena.
//
// The objects will be sorted in ascending order by arena base address.
//
// Returns ZX_ERR_OUT_OF_RANGE if |count| is 0 or |i| and |count| specify an invalid range.
//
// Returns ZX_ERR_BUFFER_TOO_SMALL if the buffer is too small.
zx_status_t pmm_get_arena_info(size_t count, uint64_t i, pmm_arena_info_t* buffer,
                               size_t buffer_size);

// flags for allocation routines below
#define PMM_ALLOC_FLAG_ANY (0 << 0)  // no restrictions on which arena to allocate from
#define PMM_ALLOC_FLAG_LO_MEM (1 << 0)  // allocate only from arenas marked LO_MEM
// the caller can handle allocation failures with a delayed page_request_t request.
#define PMM_ALLOC_DELAY_OK (1 << 1)

// Debugging flag that can be used to induce artificial delayed page allocation by randomly
// rejecting some fraction of the synchronous allocations which have PMM_ALLOC_DELAY_OK set.
#define RANDOM_DELAYED_ALLOC 0

// Allocate count pages of physical memory, adding to the tail of the passed list.
// The list must be initialized.
zx_status_t pmm_alloc_pages(size_t count, uint alloc_flags, list_node* list) __NONNULL((3));

// Allocate a single page of physical memory.
zx_status_t pmm_alloc_page(uint alloc_flags, vm_page** p) __NONNULL((2));
zx_status_t pmm_alloc_page(uint alloc_flags, paddr_t* pa) __NONNULL((2));
zx_status_t pmm_alloc_page(uint alloc_flags, vm_page** p, paddr_t* pa) __NONNULL((2, 3));

// Allocate a specific range of physical pages, adding to the tail of the passed list.
zx_status_t pmm_alloc_range(paddr_t address, size_t count, list_node* list) __NONNULL((3));

// Allocate a run of contiguous pages, aligned on log2 byte boundary (0-31).
// Return the base address of the run in the physical address pointer and
// append the allocate page structures to the tail of the passed in list.
zx_status_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t align_log2, paddr_t* pa,
                                 list_node* list) __NONNULL((4, 5));

// Fallback delayed allocation function if regular synchronous allocation fails. See
// the page_request_t struct documentation for more details.
void pmm_alloc_pages(uint alloc_flags, page_request_t* req) __NONNULL((2));

// Clears the request. Returns true if the pmm is temporarily retaining a reference
// to the request's ctx, or false otherwise. Regardless of the return value, it is
// safe to free |req| as soon as this function returns.
bool pmm_clear_request(page_request_t* req) __NONNULL((1));

// Swaps the memory used for tracking an outstanding request.
//
// The callbacks and request ctx must be identical for the two requests. As soon as
// this function returns, it is safe to release the pointer |old|.
void pmm_swap_request(page_request_t* old, page_request_t* new_req) __NONNULL((1, 2));

// Free a list of physical pages.
void pmm_free(list_node* list) __NONNULL((1));

// Free a single page.
void pmm_free_page(vm_page_t* page) __NONNULL((1));

// Return count of unallocated physical pages in system.
uint64_t pmm_count_free_pages();

// Return amount of physical memory in system, in bytes.
uint64_t pmm_count_total_bytes();

// Return the PageQueues.
PageQueues* pmm_page_queues();

// virtual to physical
paddr_t vaddr_to_paddr(const void* va);

// paddr to vm_page_t
vm_page_t* paddr_to_vm_page(paddr_t addr);

#define MAX_WATERMARK_COUNT 8

typedef void (*mem_avail_state_updated_callback_t)(void* context, uint8_t cur_state);

// Function to initialize PMM memory reclamation.
//
// |watermarks| is an array of values that delineate the memory availability states. The values
// should be monotonically increasing with intervals of at least PAGE_SIZE and its first entry must
// be larger than |debounce|. Its length is given by |watermark_count|, with a maximum of
// MAX_WATERMARK_COUNT. The pointer is not retained after this function returns.
//
// When the system has a given amount of free memory available, the memory availability state is
// defined as the index of the smallest watermark which is greater than that amount of free
// memory.  Whenever the amount of memory enters a new state, |mem_avail_state_updated_callback|
// will be invoked with the registered context and the index of the new state.
//
// Transitions are debounced by not leaving a state until the amount of memory is at least
// |debounce| bytes outside of the state. Note that large |debounce| values can cause states
// to be entirely skipped. Also note that this means that at least |debounce| bytes must be
// reclaimed before the system can transition into a healthier memory availability state.
//
// To give an example of state transitions with the watermarks [20MB, 40MB, 45MB, 55MB] and
// debounce 15MB.
//   75MB:4 -> 41MB:4 -> 40MB:2 -> 26MB:2 -> 25MB:1 -> 6MB:1 ->
//   5MB:0 -> 34MB:0 -> 35MB:1 -> 54MB:1 -> 55MB:4
//
// Invocations of |mem_avail_state_updated_callback| are fully serialized, but they occur on
// arbitrary threads when pages are being freed. As this likely occurs under important locks, the
// callback itself should not perform memory reclamation; instead it should communicate the memory
// level to a separate thread that is responsible for reclaiming memory. Furthermore, the callback
// is immediately invoked during the execution of this function with the index of the initial memory
// state.
zx_status_t pmm_init_reclamation(const uint64_t* watermarks, uint8_t watermark_count,
                                 uint64_t debounce, void* context,
                                 mem_avail_state_updated_callback_t callback);

// Should be called after the kernel command line has been parsed.
void pmm_checker_init_from_cmdline();

// Synchronously walk the PMM's free list and validate each page.  This is an incredibly expensive
// operation and should only be used for debugging purposes.
void pmm_checker_check_all_free_pages();

// Synchronously walk the PMM's free list and poison (via kASAN) each page. This is an
// incredibly expensive operation and should be used with care.
void pmm_asan_poison_all_free_pages();

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_H_
