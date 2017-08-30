// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmpctmalloc.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <err.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <lib/heap.h>
#include <platform.h>
#include <trace.h>

// Malloc implementation tuned for space.
//
// Allocation strategy takes place with a global mutex.  Freelist entries are
// kept in linked lists with 8 different sizes per binary order of magnitude
// and the header size is two words with eager coalescing on free.
//
// ## Concepts ##
//
// OS allocation:
//   A contiguous range of pages allocated from the OS using heap_page_alloc(),
//   typically via heap_grow(). Initial layout:
//
//   Low addr =>
//     header_t left_sentinel -- Marked as allocated, |left| pointer NULL.
//                               If created for a large allocation, |left|
//                               field will have LARGE_ALLOC_BIT set.
//     free_t memory_area -- Marked as free, with appropriate size,
//                           and pointed to by a free bucket.
//     [bulk of usable memory]
//     header_t right_sentinel -- Marked as allocated, size zero
//   <= High addr
//
//   For a normal allocation, the free memory area is added to the
//   appropriate free bucket and picked up later in the cmpct_alloc()
//   logic. For a large allocation, the area skips the primary free buckets
//   and is returned directly via a |free_t** bucket| param.
//
//   cmpctmalloc does not keep a list of OS allocations; each is meant to free
//   itself to the OS when all of its memory areas become free.
//
// Memory area:
//   A sub-range of an OS allocation. Used to satisfy
//   cmpct_alloc()/cmpct_memalign() calls. Can be free and live in a free
//   bucket, or can be allocated and managed by the user.
//
//   Memory areas, both free and allocated, always begin with a header_t,
//   followed by the area's usable memory. header_t.size includes the size of
//   the header. untag(header_t.left) points to the preceding area's header_t.
//
//   The low bits of header_t.left hold additional flags about the area:
//   - FREE_BIT: The area is free, and lives in a free bucket
//   - LARGE_ALLOC_BIT: The area is the left sentinel of an OS allocation
//     created by large_alloc()
//   These bits shouldn't be checked directly; use the is_tagged_as_*()
//   functions.
//
//   If the area is free (is_tagged_as_free(header_t*)), the area's header
//   includes the doubly-linked free list pointers defined by free_t (which is a
//   header_t overlay). Those pointers are used to chain the free area off of
//   the appropriately-sized free bucket.
//
// Normal (small/non-large) allocation:
//   An alloction of less than HEAP_LARGE_ALLOC_BYTES, which can fit in a free
//   bucket.
//
// Large allocation:
//   An alloction of more than HEAP_LARGE_ALLOC_BYTES. Won't fit in any free
//   buckets, so it always creates a new OS allocation.
//
// Free buckets:
//   Freelist entries are kept in linked lists with 8 different sizes per binary
//   order of magnitude: heap.free_lists[NUMBER_OF_BUCKETS]
//
//   Allocations are always rounded up to the nearest bucket size. This would
//   appear to waste memory, but in fact it avoids some fragmentation.
//
//   Consider two buckets with size 512 and 576 (512 + 64). Perhaps the program
//   often allocates 528 byte objects for some reason. When we need to allocate
//   528 bytes, we round that up to 576 bytes. When it is freed, it goes in the
//   576 byte bucket, where it is available for the next of the common 528 byte
//   allocations.
//
//   If we did not round up allocations, then (assuming no coalescing is
//   possible) we would have to place the freed 528 bytes in the 512 byte
//   bucket, since only memory areas greater than or equal to 576 bytes can go
//   in the 576 byte bucket. The next time we need to allocate a 528 byte object
//   we do not look in the 512 byte bucket, because we want to be sure the first
//   memory area we look at is big enough, to avoid searching a long chain of
//   just-too-small memory areas on the free list. We would not find the 528
//   byte space and would have to carve out a new 528 byte area from a large
//   free memory area, making fragmentation worse.
//
// cmpct_free() behavior:
//   Freed memory areas are eagerly coalesced with free left/right neighbors. If
//   the new free area covers an entire OS allocation (i.e., its left and right
//   neighbors are both sentinels), the OS allocation is returned to the OS.
//
//   Exception: to avoid OS free/alloc churn when right on the edge, the heap
//   will try to hold onto one entirely-free, non-large OS allocation instead of
//   returning it to the OS. See cached_os_alloc.

#if defined(DEBUG) || LK_DEBUGLEVEL > 2
#define CMPCT_DEBUG
#endif

#define LOCAL_TRACE 0

#define ALLOC_FILL 0x99
#define FREE_FILL 0x77
#define PADDING_FILL 0x55

#if !defined(HEAP_GROW_SIZE)
#define HEAP_GROW_SIZE (1 * 1024 * 1024) /* Grow aggressively */
#endif

static_assert(IS_PAGE_ALIGNED(HEAP_GROW_SIZE), "");

// Individual allocations above 4Mbytes are just fetched directly from the
// block allocator.
#define HEAP_ALLOC_VIRTUAL_BITS 22
#define HEAP_LARGE_ALLOC_BYTES (1u << HEAP_ALLOC_VIRTUAL_BITS)

// When we grow the heap we have to have somewhere in the freelist to put the
// resulting freelist entry, so the freelist has to have a certain number of
// buckets.
static_assert(HEAP_GROW_SIZE <= HEAP_LARGE_ALLOC_BYTES, "");

// Buckets for allocations.  The smallest 15 buckets are 8, 16, 24, etc. up to
// 120 bytes.  After that we round up to the nearest size that can be written
// /^0*1...0*$/, giving 8 buckets per order of binary magnitude.  The freelist
// entries in a given bucket have at least the given size, plus the header
// size.  On 64 bit, the 8 byte bucket is useless, since the freelist header
// is 16 bytes larger than the header, but we have it for simplicity.
#define NUMBER_OF_BUCKETS (1 + 15 + (HEAP_ALLOC_VIRTUAL_BITS - 7) * 8)

// If a header's |left| field has this bit set, it is free and lives in
// a free bucket.
#define FREE_BIT (1 << 0)

// If a left sentinel's |left| field has this bit set, it is the start
// of an OS allocation that was requested by large_alloc().
#define LARGE_ALLOC_BIT (1 << 1)

#define HEADER_LEFT_BIT_MASK (FREE_BIT | LARGE_ALLOC_BIT)

// All individual memory areas on the heap start with this.
typedef struct header_struct {
    // Pointer to the previous area in memory order. The lower two bits are used
    // to store extra state: see FREE_BIT, LARGE_ALLOC_BIT. The left sentinel
    // will have NULL in the address portion of this field, and may have
    // LARGE_ALLOC_BIT set. Left and right sentinels will always be marked as
    // "allocated" to avoid coalescing.
    struct header_struct* left;
    // The size of the memory area in bytes, including this header.
    // The right sentinel will have 0 in this field.
    size_t size;
} header_t;

typedef struct free_struct {
    header_t header;
    struct free_struct* next;
    struct free_struct* prev;
} free_t;

struct heap {
    // Total bytes allocated from the OS for the heap.
    size_t size;

    // Bytes of usable free space in the heap.
    size_t remaining;

    // A non-large OS allocation that could have been freed to the OS but
    // wasn't. We will attempt to use this before allocating more memory from
    // the OS, to reduce churn. May be null. If non-null, cached_os_alloc->size
    // holds the total size allocated from the OS for this block.
    header_t* cached_os_alloc;

    // Guards all elements in this structure. See lock(), unlock().
    mutex_t lock;

    // Free lists, bucketed by size. See size_to_index_helper().
    free_t* free_lists[NUMBER_OF_BUCKETS];

    // Bitmask that tracks whether a given free_lists entry has any elements.
    // See set_free_list_bit(), clear_free_list_bit().
#define BUCKET_WORDS (((NUMBER_OF_BUCKETS) + 31) >> 5)
    uint32_t free_list_bits[BUCKET_WORDS];
};

// Heap static vars.
static struct heap theheap;

static ssize_t heap_grow(size_t len, free_t** bucket);

static void lock(void) TA_ACQ(theheap.lock) {
    mutex_acquire(&theheap.lock);
}

static void unlock(void) TA_REL(theheap.lock) {
    mutex_release(&theheap.lock);
}

static void dump_free(header_t* header) {
    dprintf(INFO, "\t\tbase %p, end %#" PRIxPTR ", len %#zx (%zu)\n",
            header, (vaddr_t)header + header->size, header->size, header->size);
}

void cmpct_dump(bool panic_time) TA_NO_THREAD_SAFETY_ANALYSIS {
    if (!panic_time) {
        lock();
    }

    dprintf(INFO, "Heap dump (using cmpctmalloc):\n");
    dprintf(INFO, "\tsize %lu, remaining %lu\n",
            (unsigned long)theheap.size,
            (unsigned long)theheap.remaining);

    dprintf(INFO, "\tfree list:\n");
    for (int i = 0; i < NUMBER_OF_BUCKETS; i++) {
        bool header_printed = false;
        free_t* free_area = theheap.free_lists[i];
        for (; free_area != NULL; free_area = free_area->next) {
            ASSERT(free_area != free_area->next);
            if (!header_printed) {
                dprintf(INFO, "\tbucket %d\n", i);
                header_printed = true;
            }
            dump_free(&free_area->header);
        }
    }

    if (!panic_time) {
        unlock();
    }
}

void cmpct_get_info(size_t* size_bytes, size_t* free_bytes) {
    lock();
    *size_bytes = theheap.size;
    *free_bytes = theheap.remaining;
    unlock();
}

// Operates in sizes that don't include the allocation header;
// i.e., the usable portion of a memory area.
static int size_to_index_helper(
    size_t size, size_t* rounded_up_out, int adjust, int increment) {
    // First buckets are simply 8-spaced up to 128.
    if (size <= 128) {
        if (sizeof(size_t) == 8u && size <= sizeof(free_t) - sizeof(header_t)) {
            *rounded_up_out = sizeof(free_t) - sizeof(header_t);
        } else {
            *rounded_up_out = size;
        }
        // No allocation is smaller than 8 bytes, so the first bucket is for 8
        // byte spaces (not including the header).  For 64 bit, the free list
        // struct is 16 bytes larger than the header, so no allocation can be
        // smaller than that (otherwise how to free it), but we have empty 8
        // and 16 byte buckets for simplicity.
        return (size >> 3) - 1;
    }

    // We are going to go up to the next size to round up, but if we hit a
    // bucket size exactly we don't want to go up. By subtracting 8 here, we
    // will do the right thing (the carry propagates up for the round numbers
    // we are interested in).
    size += adjust;
    // After 128 the buckets are logarithmically spaced, every 16 up to 256,
    // every 32 up to 512 etc.  This can be thought of as rows of 8 buckets.
    // GCC intrinsic count-leading-zeros.
    // Eg. 128-255 has 24 leading zeros and we want row to be 4.
    unsigned row = sizeof(size_t) * 8 - 4 - __builtin_clzl(size);
    // For row 4 we want to shift down 4 bits.
    unsigned column = (size >> row) & 7;
    int row_column = (row << 3) | column;
    row_column += increment;
    size = (8 + (row_column & 7)) << (row_column >> 3);
    *rounded_up_out = size;
    // We start with 15 buckets, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96,
    // 104, 112, 120.  Then we have row 4, sizes 128 and up, with the
    // row-column 8 and up.
    int answer = row_column + 15 - 32;
    DEBUG_ASSERT(answer < NUMBER_OF_BUCKETS);
    return answer;
}

// Round up size to next bucket when allocating.
static int size_to_index_allocating(size_t size, size_t* rounded_up_out) {
    size_t rounded = ROUNDUP(size, 8);
    return size_to_index_helper(rounded, rounded_up_out, -8, 1);
}

// Round down size to next bucket when freeing.
static int size_to_index_freeing(size_t size) {
    size_t dummy;
    return size_to_index_helper(size, &dummy, 0, 0);
}

static inline header_t* tag_as_free(void* left) {
    return (header_t*)((uintptr_t)left | FREE_BIT);
}

// Returns true if this header_t is marked as free.
static inline bool is_tagged_as_free(const header_t* header) {
    // The free bit is stashed in the lower bit of header->left.
    return ((uintptr_t)(header->left) & FREE_BIT) != 0;
}

// Set on the left sentinel of OS allocations created by large_alloc().
static inline header_t* tag_as_large(const void* left) {
    return (header_t*)((uintptr_t)left | LARGE_ALLOC_BIT);
}

static inline bool is_tagged_as_large(const header_t* header) {
    return ((uintptr_t)(header->left) & LARGE_ALLOC_BIT) != 0;
}

static inline header_t* untag(const void* left) {
    return (header_t*)((uintptr_t)left & ~HEADER_LEFT_BIT_MASK);
}

static inline header_t* right_header(header_t* header) {
    return (header_t*)((char*)header + header->size);
}

static inline void set_free_list_bit(int index) {
    theheap.free_list_bits[index >> 5] |= (1u << (31 - (index & 0x1f)));
}

static inline void clear_free_list_bit(int index) {
    theheap.free_list_bits[index >> 5] &= ~(1u << (31 - (index & 0x1f)));
}

static int find_nonempty_bucket(int index) {
    uint32_t mask = (1u << (31 - (index & 0x1f))) - 1;
    mask = mask * 2 + 1;
    mask &= theheap.free_list_bits[index >> 5];
    if (mask != 0) {
        return (index & ~0x1f) + __builtin_clz(mask);
    }
    for (index = ROUNDUP(index + 1, 32);
         index <= NUMBER_OF_BUCKETS; index += 32) {
        mask = theheap.free_list_bits[index >> 5];
        if (mask != 0u) {
            return index + __builtin_clz(mask);
        }
    }
    return -1;
}

static bool is_start_of_os_allocation(const header_t* header) {
    return untag(header->left) == untag(NULL);
}

static void create_free_area(
    void* address, void* left, size_t size, free_t** bucket) {

    free_t* free_area = (free_t*)address;
    free_area->header.size = size;
    free_area->header.left = tag_as_free(left);
    if (bucket == NULL) {
        int index = size_to_index_freeing(size - sizeof(header_t));
        set_free_list_bit(index);
        bucket = &theheap.free_lists[index];
    }
    free_t* old_head = *bucket;
    if (old_head != NULL) {
        old_head->prev = free_area;
    }
    free_area->next = old_head;
    free_area->prev = NULL;
    *bucket = free_area;
    theheap.remaining += size;
#ifdef CMPCT_DEBUG
    memset(free_area + 1, FREE_FILL, size - sizeof(free_t));
#endif
}

static bool is_end_of_os_allocation(char* address) {
    return ((header_t*)address)->size == 0;
}

static void free_to_os(void* ptr, size_t size) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(ptr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
    heap_page_free(ptr, size >> PAGE_SIZE_SHIFT);
    theheap.size -= size;
}

// May call free_to_os(), or may cache the (non-large) OS allocation in
// cached_os_alloc. |left_sentinel| is the start of the OS allocation, and
// |total_size| is the (page-aligned) number of bytes that were originally
// allocated from the OS.
static void possibly_free_to_os(header_t *left_sentinel, size_t total_size) {
    if (theheap.cached_os_alloc == NULL && !is_tagged_as_large(left_sentinel)) {
        LTRACEF("Keeping 0x%zx-byte OS alloc @%p\n", total_size, left_sentinel);
        theheap.cached_os_alloc = left_sentinel;
        theheap.cached_os_alloc->left = NULL;
        theheap.cached_os_alloc->size = total_size;
    } else {
        LTRACEF("Returning 0x%zx bytes @%p to OS (%s)\n",
                total_size, left_sentinel,
                is_tagged_as_large(left_sentinel) ? "large" : "small");
        free_to_os(left_sentinel, total_size);
    }
}

// Frees |size| bytes starting at |address|, either to a free bucket or to the
// OS (in which case the left/right sentinels are freed as well). |address|
// should point to what would be the header_t of the memory area to free, and
// |left| and |size| should be set to the values that the header_t would have
// contained. This is broken out because the header_t will not contain the
// proper size when coalescing neighboring areas.
static void free_memory(void* address, void* left, size_t size) {
    left = untag(left);
    if (IS_PAGE_ALIGNED(left) &&
        is_start_of_os_allocation(left) &&
        is_end_of_os_allocation((char*)address + size)) {

        // Assert that it's safe to do a simple 2*sizeof(header_t)) below.
        DEBUG_ASSERT_MSG(((header_t*)left)->size == sizeof(header_t),
                         "Unexpected left sentinel size %zu != header size %zu",
                         ((header_t*)left)->size, sizeof(header_t));
        possibly_free_to_os((header_t*)left, size + 2 * sizeof(header_t));
    } else {
        create_free_area(address, left, size, NULL);
    }
}

static void unlink_free(free_t* free_area, int bucket) {
    theheap.remaining -= free_area->header.size;
    ASSERT(theheap.remaining < 4000000000u);
    free_t* next = free_area->next;
    free_t* prev = free_area->prev;
    if (theheap.free_lists[bucket] == free_area) {
        theheap.free_lists[bucket] = next;
        if (next == NULL) {
            clear_free_list_bit(bucket);
        }
    }
    if (prev != NULL) {
        prev->next = next;
    }
    if (next != NULL) {
        next->prev = prev;
    }
}

static void unlink_free_unknown_bucket(free_t* free_area) {
    return unlink_free(
        free_area,
        size_to_index_freeing(free_area->header.size - sizeof(header_t)));
}

static void* create_allocation_header(
    void* address, size_t offset, size_t size, void* left) {

    header_t* standalone = (header_t*)((char*)address + offset);
    standalone->left = untag(left);
    standalone->size = size;
    return standalone + 1;
}

static void FixLeftPointer(header_t* right, header_t* new_left) {
    int tag = (uintptr_t)right->left & 1;
    right->left = (header_t*)(((uintptr_t)new_left & ~1) | tag);
}

static void WasteFreeMemory(void) {
    while (theheap.remaining != 0) {
        cmpct_alloc(1);
    }
}

// If we just make a big allocation it gets rounded off.  If we actually
// want to use a reasonably accurate amount of memory for test purposes, we
// have to do many small allocations.
static void* TestTrimHelper(ssize_t target) {
    char* answer = NULL;
    size_t remaining = theheap.remaining;
    while (theheap.remaining - target > 512) {
        char* next_block = cmpct_alloc(8 + ((theheap.remaining - target) >> 2));
        *(char**)next_block = answer;
        answer = next_block;
        if (theheap.remaining > remaining) {
            return answer;
        }
        // Abandon attempt to hit particular freelist entry size if we
        // accidentally got more memory from the OS.
        remaining = theheap.remaining;
    }
    return answer;
}

static void TestTrimFreeHelper(char* block) {
    while (block) {
        char* next_block = *(char**)block;
        cmpct_free(block);
        block = next_block;
    }
}

static void cmpct_test_trim(void) {
    // XXX: Re-enable this test if we want, disabled due to float math
    return;
    WasteFreeMemory();

    size_t test_sizes[200];
    int sizes = 0;

    for (size_t s = 1; s < PAGE_SIZE * 4; s = (s + 1) * 1.1) {
        test_sizes[sizes++] = s;
        ASSERT(sizes < 200);
    }
    for (ssize_t s = -32; s <= 32; s += 8) {
        test_sizes[sizes++] = PAGE_SIZE + s;
        ASSERT(sizes < 200);
    }

    // Test allocations at the start of an OS allocation.
    for (int with_second_alloc = 0;
         with_second_alloc < 2; with_second_alloc++) {
        for (int i = 0; i < sizes; i++) {
            size_t s = test_sizes[i];

            char *a, *a2 = NULL;
            a = cmpct_alloc(s);
            if (with_second_alloc) {
                a2 = cmpct_alloc(1);
                if (s<PAGE_SIZE>> 1) {
                    // It is the intention of the test that a is at the start
                    // of an OS allocation and that a2 is "right after" it.
                    // Otherwise we are not testing what I thought. OS
                    // allocations are certainly not smaller than a page, so
                    // check in that case.
                    ASSERT((uintptr_t)(a2 - a) < s * 1.13 + 48);
                }
            }
            cmpct_trim();
            size_t remaining = theheap.remaining;
            // We should have < 1 page on either side of the a allocation.
            ASSERT(remaining < PAGE_SIZE * 2);
            cmpct_free(a);
            if (with_second_alloc) {
                // Now only a2 is holding onto the OS allocation.
                ASSERT(theheap.remaining > remaining);
            } else {
                ASSERT(theheap.remaining == 0);
            }
            remaining = theheap.remaining;
            cmpct_trim();
            ASSERT(theheap.remaining <= remaining);
            // If a was at least one page then the trim should have freed up
            // that page.
            if (s >= PAGE_SIZE && with_second_alloc) {
                ASSERT(theheap.remaining < remaining);
            }
            if (with_second_alloc) {
                cmpct_free(a2);
            }
        }
        ASSERT(theheap.remaining == 0);
    }

    ASSERT(theheap.remaining == 0);

    // Now test allocations near the end of an OS allocation.
    for (ssize_t wobble = -64; wobble <= 64; wobble += 8) {
        for (int i = 0; i < sizes; i++) {
            size_t s = test_sizes[i];

            if ((ssize_t)s + wobble < 0) {
                continue;
            }

            char* start_of_os_alloc = cmpct_alloc(1);

            // If the OS allocations are very small this test does not make
            // sense.
            if (theheap.remaining <= s + wobble) {
                cmpct_free(start_of_os_alloc);
                continue;
            }

            char* big_bit_in_the_middle = TestTrimHelper(s + wobble);
            size_t remaining = theheap.remaining;

            // If the remaining is big we started a new OS allocation and the
            // test makes no sense.
            if (remaining > 128 + s * 1.13 + wobble) {
                cmpct_free(start_of_os_alloc);
                TestTrimFreeHelper(big_bit_in_the_middle);
                continue;
            }

            cmpct_free(start_of_os_alloc);
            remaining = theheap.remaining;

            // This trim should sometimes trim a page off the end of the OS
            // allocation.
            cmpct_trim();
            ASSERT(theheap.remaining <= remaining);
            remaining = theheap.remaining;

            // We should have < 1 page on either side of the big allocation.
            ASSERT(remaining < PAGE_SIZE * 2);

            TestTrimFreeHelper(big_bit_in_the_middle);
        }
    }
}

static void cmpct_test_buckets(void) {
    size_t rounded;
    unsigned bucket;
    // Check for the 8-spaced buckets up to 128.
    for (unsigned i = 1; i <= 128; i++) {
        // Round up when allocating.
        bucket = size_to_index_allocating(i, &rounded);
        unsigned expected = (ROUNDUP(i, 8) >> 3) - 1;
        ASSERT(bucket == expected);
        ASSERT(IS_ALIGNED(rounded, 8));
        ASSERT(rounded >= i);
        if (i >= sizeof(free_t) - sizeof(header_t)) {
            // Once we get above the size of the free area struct (4 words), we
            // won't round up much for these small size.
            ASSERT(rounded - i < 8);
        }
        // Only rounded sizes are freed.
        if ((i & 7) == 0) {
            // Up to size 128 we have exact buckets for each multiple of 8.
            ASSERT(bucket == (unsigned)size_to_index_freeing(i));
        }
    }
    int bucket_base = 7;
    for (unsigned j = 16; j < 1024; j *= 2, bucket_base += 8) {
        // Note the "<=", which ensures that we test the powers of 2 twice to
        // ensure that both ways of calculating the bucket number match.
        for (unsigned i = j * 8; i <= j * 16; i++) {
            // Round up to j multiple in this range when allocating.
            bucket = size_to_index_allocating(i, &rounded);
            unsigned expected = bucket_base + ROUNDUP(i, j) / j;
            ASSERT(bucket == expected);
            ASSERT(IS_ALIGNED(rounded, j));
            ASSERT(rounded >= i);
            ASSERT(rounded - i < j);
            // Only 8-rounded sizes are freed or chopped off the end of a free
            // area when allocating.
            if ((i & 7) == 0) {
                // When freeing, if we don't hit the size of the bucket
                // precisely, we have to put the free space into a smaller
                // bucket, because the buckets have entries that will always
                // be big enough for the corresponding allocation size (so we
                // don't have to traverse the free chains to find a big enough
                // one).
                if ((i % j) == 0) {
                    ASSERT((int)bucket == size_to_index_freeing(i));
                } else {
                    ASSERT((int)bucket - 1 == size_to_index_freeing(i));
                }
            }
        }
    }
}

static void cmpct_test_get_back_newly_freed_helper(size_t size) {
    void* allocated = cmpct_alloc(size);
    if (allocated == NULL) {
        return;
    }
    char* allocated2 = cmpct_alloc(8);
    char* expected_position = (char*)allocated + size;
    if (allocated2 < expected_position ||
        allocated2 > expected_position + 128) {
        // If the allocated2 allocation is not in the same OS allocation as the
        // first allocation then the test may not work as expected (the memory
        // may be returned to the OS when we free the first allocation, and we
        // might not get it back).
        cmpct_free(allocated);
        cmpct_free(allocated2);
        return;
    }

    cmpct_free(allocated);
    void* allocated3 = cmpct_alloc(size);
    // To avoid churn and fragmentation we would want to get the newly freed
    // memory back again when we allocate the same size shortly after.
    ASSERT(allocated3 == allocated);
    cmpct_free(allocated2);
    cmpct_free(allocated3);
}

static void cmpct_test_get_back_newly_freed(void) {
    size_t increment = 16;
    for (size_t i = 128; i <= 0x8000000; i *= 2, increment *= 2) {
        for (size_t j = i; j < i * 2; j += increment) {
            cmpct_test_get_back_newly_freed_helper(i - 8);
            cmpct_test_get_back_newly_freed_helper(i);
            cmpct_test_get_back_newly_freed_helper(i + 1);
        }
    }
    for (size_t i = 1024; i <= 2048; i++) {
        cmpct_test_get_back_newly_freed_helper(i);
    }
}

static void cmpct_test_return_to_os(void) {
    cmpct_trim();
    size_t remaining = theheap.remaining;
    // This goes in a new OS allocation since the trim above removed any free
    // area big enough to contain it.
    void* a = cmpct_alloc(5000);
    void* b = cmpct_alloc(2500);
    cmpct_free(a);
    cmpct_free(b);
    // If things work as expected the new allocation is at the start of an OS
    // allocation.  There's just one sentinel and one header to the left of it.
    // It that's not the case then the allocation was met from some space in
    // the middle of an OS allocation, and our test won't work as expected, so
    // bail out.
    if (((uintptr_t)a & (PAGE_SIZE - 1)) != sizeof(header_t) * 2) {
        return;
    }
    // No trim needed when the entire OS allocation is free.
    ASSERT(remaining == theheap.remaining);
}

void cmpct_test(void) {
    cmpct_test_buckets();
    cmpct_test_get_back_newly_freed();
    cmpct_test_return_to_os();
    cmpct_test_trim();
    cmpct_dump(false);
    void* ptr[16];

    ptr[0] = cmpct_alloc(8);
    ptr[1] = cmpct_alloc(32);
    ptr[2] = cmpct_alloc(7);
    cmpct_trim();
    ptr[3] = cmpct_alloc(0);
    ptr[4] = cmpct_alloc(98713);
    ptr[5] = cmpct_alloc(16);

    cmpct_free(ptr[5]);
    cmpct_free(ptr[1]);
    cmpct_free(ptr[3]);
    cmpct_free(ptr[0]);
    cmpct_free(ptr[4]);
    cmpct_free(ptr[2]);

    cmpct_dump(false);
    cmpct_trim();
    cmpct_dump(false);

    int i;
    for (i = 0; i < 16; i++)
        ptr[i] = 0;

    for (i = 0; i < 32768; i++) {
        unsigned int index = (unsigned int)rand() % 16;

        if ((i % (16 * 1024)) == 0) {
            printf("pass %d\n", i);
        }

        // printf("index 0x%x\n", index);
        if (ptr[index]) {
            // printf("freeing ptr[0x%x] = %p\n", index, ptr[index]);
            cmpct_free(ptr[index]);
            ptr[index] = 0;
        }
        unsigned int align = 1 << ((unsigned int)rand() % 8);
        ptr[index] = cmpct_memalign((unsigned int)rand() % 32768, align);
        // printf("ptr[0x%x] = %p, align 0x%x\n", index, ptr[index], align);

        DEBUG_ASSERT(((addr_t)ptr[index] % align) == 0);
        // cmpct_dump(false);
    }

    for (i = 0; i < 16; i++) {
        if (ptr[i]) {
            cmpct_free(ptr[i]);
        }
    }

    cmpct_dump(false);
}

static void check_free_fill(void* ptr, size_t size) {
    // The first 16 bytes of the region won't have free fill due to overlap
    // with the allocator bookkeeping.
    const size_t start = sizeof(free_t) - sizeof(header_t);
    for (size_t i = start; i < size; ++i) {
        uint8_t byte = ((uint8_t*)ptr)[i];
        if (byte != FREE_FILL) {
            platform_panic_start();
            printf("Heap free fill check fail.  Allocated region:\n");
            hexdump8(ptr, size);
            panic("allocating %lu bytes, fill was %02x, offset %lu\n",
                  size, byte, i);
        }
    }
}

static void* large_alloc(size_t size) {
#ifdef CMPCT_DEBUG
    size_t requested_size = size;
#endif
    size = ROUNDUP(size, 8);
    free_t* free_area = NULL;
    lock();
    if (heap_grow(size, &free_area) < 0) {
        unlock();
        return NULL;
    }
    // Passing a non-null |bucket| value will cause heap_grow() to tag the new
    // memory's left sentinel as as large allocation.
    DEBUG_ASSERT(is_tagged_as_large(untag(free_area->header.left)));

    void* result = create_allocation_header(
        free_area, 0, free_area->header.size, free_area->header.left);
    // Normally the 'remaining free space' counter would be decremented when we
    // unlink the free area from its bucket.  However in this case the free
    // area was too big to go in any bucket and we had it in our own
    // "free_area" variable so there is no unlinking and we have to adjust the
    // counter here.
    theheap.remaining -= free_area->header.size;
    unlock();
#ifdef CMPCT_DEBUG
    check_free_fill(result, requested_size);
    memset(result, ALLOC_FILL, requested_size);
    memset((char*)result + requested_size, PADDING_FILL,
           free_area->header.size - requested_size);
#endif
    return result;
}

void cmpct_trim(void) {
    // Look at free list entries that are at least as large as one page plus a
    // header. They might be at the start or the end of a block, so we can trim
    // them and free the page(s).
    lock();
    for (int bucket = size_to_index_freeing(PAGE_SIZE);
         bucket < NUMBER_OF_BUCKETS;
         bucket++) {
        free_t* next;
        for (free_t* free_area = theheap.free_lists[bucket];
             free_area != NULL;
             free_area = next) {
            DEBUG_ASSERT(
                free_area->header.size >= PAGE_SIZE + sizeof(header_t));
            next = free_area->next;
            header_t* right = right_header(&free_area->header);
            if (is_end_of_os_allocation((char*)right)) {
                char* old_os_allocation_end =
                    (char*)ROUNDUP((uintptr_t)right, PAGE_SIZE);
                // The page will end with a smaller free list entry and a
                // header-sized sentinel.
                char* new_os_allocation_end =
                    (char*)ROUNDUP(
                        (uintptr_t)free_area +
                            sizeof(header_t) +
                            sizeof(free_t),
                        PAGE_SIZE);
                size_t freed_up = old_os_allocation_end - new_os_allocation_end;
                DEBUG_ASSERT(IS_PAGE_ALIGNED(freed_up));
                // Rare, because we only look at large freelist entries, but
                // unlucky rounding could mean we can't actually free anything
                // here.
                if (freed_up == 0) {
                    continue;
                }
                unlink_free(free_area, bucket);
                size_t new_free_size = free_area->header.size - freed_up;
                DEBUG_ASSERT(new_free_size >= sizeof(free_t));
                // Right sentinel, not free, stops attempts to coalesce right.
                create_allocation_header(
                    free_area, new_free_size, 0, free_area);
                // Also puts it in the correct bucket.
                create_free_area(free_area, untag(free_area->header.left),
                                 new_free_size, NULL);
                heap_page_free(new_os_allocation_end,
                               freed_up >> PAGE_SIZE_SHIFT);
                theheap.size -= freed_up;
            } else if (is_start_of_os_allocation(
                           untag(free_area->header.left))) {
                char* old_os_allocation_start =
                    (char*)ROUNDDOWN((uintptr_t)free_area, PAGE_SIZE);
                // For the sentinel, we need at least one header-size of space
                // between the page edge and the first allocation to the right
                // of the free area.
                char* new_os_allocation_start =
                    (char*)ROUNDDOWN((uintptr_t)(right - 1), PAGE_SIZE);
                size_t freed_up =
                    new_os_allocation_start - old_os_allocation_start;
                DEBUG_ASSERT(IS_PAGE_ALIGNED(freed_up));
                // This should not happen because we only look at the large
                // free list buckets.
                if (freed_up == 0) {
                    continue;
                }
                unlink_free(free_area, bucket);
                size_t sentinel_size = sizeof(header_t);
                size_t new_free_size = free_area->header.size - freed_up;
                if (new_free_size < sizeof(free_t)) {
                    sentinel_size += new_free_size;
                    new_free_size = 0;
                }
                // Left sentinel, not free, stops attempts to coalesce left.
                create_allocation_header(new_os_allocation_start, 0,
                                         sentinel_size, NULL);
                if (new_free_size == 0) {
                    FixLeftPointer(right, (header_t*)new_os_allocation_start);
                } else {
                    DEBUG_ASSERT(new_free_size >= sizeof(free_t));
                    char* new_free = new_os_allocation_start + sentinel_size;
                    // Also puts it in the correct bucket.
                    create_free_area(new_free, new_os_allocation_start,
                                     new_free_size, NULL);
                    FixLeftPointer(right, (header_t*)new_free);
                }
                heap_page_free(old_os_allocation_start,
                               freed_up >> PAGE_SIZE_SHIFT);
                theheap.size -= freed_up;
            }
        }
    }
    unlock();
}

void* cmpct_alloc(size_t size) {
    if (size == 0u) {
        return NULL;
    }

    // TODO(dbort): Look into the large vs. small threshold. A "small"
    // allocation of 0x3ff000 and a "large" allocation of 0x400000 will both
    // allocate 0x401000 bytes from the OS; seems like there should be a sharper
    // distinction. The problem seems to be that growby is rounded up to a
    // bucket size, then heap_grow adds 2*header_t and rounds up to a page.
    if (size + sizeof(header_t) > HEAP_LARGE_ALLOC_BYTES) {
        return large_alloc(size);
    }

    size_t rounded_up;
    int start_bucket = size_to_index_allocating(size, &rounded_up);

    rounded_up += sizeof(header_t);

    lock();
    int bucket = find_nonempty_bucket(start_bucket);
    if (bucket == -1) {
        // Grow heap by at least 12% if we can.
        size_t growby = MIN(HEAP_LARGE_ALLOC_BYTES,
                            MAX(theheap.size >> 3,
                                MAX(HEAP_GROW_SIZE, rounded_up)));
        // Try to add a new OS allocation to the heap, reducing the size until
        // we succeed or get too small.
        while (heap_grow(growby, NULL) < 0) {
            if (growby <= rounded_up) {
                unlock();
                return NULL;
            }
            growby = MAX(growby >> 1, rounded_up);
        }
        bucket = find_nonempty_bucket(start_bucket);
    }
    free_t* head = theheap.free_lists[bucket];
    size_t left_over = head->header.size - rounded_up;
    // We can't carve off the rest for a new free space if it's smaller than the
    // free-list linked structure.  We also don't carve it off if it's less than
    // 1.6% the size of the allocation.  This is to avoid small long-lived
    // allocations being placed right next to large allocations, hindering
    // coalescing and returning pages to the OS.
    if (left_over >= sizeof(free_t) && left_over > (size >> 6)) {
        header_t* right = right_header(&head->header);
        unlink_free(head, bucket);
        void* free = (char*)head + rounded_up;
        create_free_area(free, head, left_over, NULL);
        FixLeftPointer(right, (header_t*)free);
        head->header.size -= left_over;
    } else {
        unlink_free(head, bucket);
    }
    void* result =
        create_allocation_header(head, 0, head->header.size, head->header.left);
#ifdef CMPCT_DEBUG
    check_free_fill(result, size);
    memset(result, ALLOC_FILL, size);
    memset(((char*)result) + size, PADDING_FILL,
           rounded_up - size - sizeof(header_t));
#endif
    unlock();
    return result;
}

void* cmpct_memalign(size_t size, size_t alignment) {
    if (alignment < 8) {
        return cmpct_alloc(size);
    }
    size_t padded_size =
        size + alignment + sizeof(free_t) + sizeof(header_t);
    char* unaligned = (char*)cmpct_alloc(padded_size);
    lock();
    size_t mask = alignment - 1;
    uintptr_t payload_int = (uintptr_t)unaligned + sizeof(free_t) +
                            sizeof(header_t) + mask;
    char* payload = (char*)(payload_int & ~mask);
    if (unaligned != payload) {
        header_t* unaligned_header = (header_t*)unaligned - 1;
        header_t* header = (header_t*)payload - 1;
        size_t left_over = payload - unaligned;
        create_allocation_header(
            header, 0, unaligned_header->size - left_over, unaligned_header);
        header_t* right = right_header(unaligned_header);
        unaligned_header->size = left_over;
        FixLeftPointer(right, header);
        unlock();
        cmpct_free(unaligned);
    } else {
        unlock();
    }
    // TODO: Free the part after the aligned allocation.
    return payload;
}

void cmpct_free(void* payload) {
    if (payload == NULL) {
        return;
    }
    header_t* header = (header_t*)payload - 1;
    DEBUG_ASSERT(!is_tagged_as_free(header)); // Double free!
    size_t size = header->size;
    lock();
    header_t* left = header->left;
    if (left != NULL && is_tagged_as_free(left)) {
        // Coalesce with left free object.
        unlink_free_unknown_bucket((free_t*)left);
        header_t* right = right_header(header);
        if (is_tagged_as_free(right)) {
            // Coalesce both sides.
            unlink_free_unknown_bucket((free_t*)right);
            header_t* right_right = right_header(right);
            FixLeftPointer(right_right, left);
            free_memory(left, left->left, left->size + size + right->size);
        } else {
            // Coalesce only left.
            FixLeftPointer(right, left);
            free_memory(left, left->left, left->size + size);
        }
    } else {
        header_t* right = right_header(header);
        if (is_tagged_as_free(right)) {
            // Coalesce only right.
            header_t* right_right = right_header(right);
            unlink_free_unknown_bucket((free_t*)right);
            FixLeftPointer(right_right, header);
            free_memory(header, left, size + right->size);
        } else {
            free_memory(header, left, size);
        }
    }
    unlock();
}

void* cmpct_realloc(void* payload, size_t size) {
    if (payload == NULL) {
        return cmpct_alloc(size);
    }
    header_t* header = (header_t*)payload - 1;
    size_t old_size = header->size - sizeof(header_t);
    void* new_payload = cmpct_alloc(size);
    memcpy(new_payload, payload, MIN(size, old_size));
    cmpct_free(payload);
    return new_payload;
}

static void add_to_heap(void* new_area, size_t size, free_t** bucket) {
    void* top = (char*)new_area + size;

    // Set up the left sentinel. Its |left| field will not have FREE_BIT set,
    // stopping attempts to coalesce left.
    header_t* left_sentinel = (header_t*)new_area;
    create_allocation_header(left_sentinel, 0, sizeof(header_t), NULL);
    if (bucket != NULL) {
        // If a bucket was provided, we were called by large_alloc(). Mark this
        // OS allocation as large so we can handle it properly when freeing it.
        left_sentinel->left = tag_as_large(left_sentinel->left);
    }

    // Set up the usable memory area, which will be marked free.
    header_t* new_header = left_sentinel + 1;
    size_t free_size = size - 2 * sizeof(header_t);
    create_free_area(new_header, left_sentinel, free_size, bucket);

    // Set up the right sentinel. Its |left| field will not have FREE_BIT bit
    // set, stopping attempts to coalesce right.
    header_t* right_sentinel = (header_t*)(top - sizeof(header_t));
    create_allocation_header(right_sentinel, 0, 0, new_header);
}

// Create a new free-list entry of at least size bytes (including the
// allocation header).  Called with the lock, apart from during init.
static ssize_t heap_grow(size_t size, free_t** bucket) {
    // The new free list entry will have a header on each side (the
    // sentinels) so we need to grow the gross heap size by this much more.
    size += 2 * sizeof(header_t);
    size = ROUNDUP(size, PAGE_SIZE);

    void* ptr = NULL;
    if (bucket == NULL) {
        // This is a non-large allocation request. Try to use a cached OS
        // allocation if present.
        header_t* os_alloc = (header_t*)theheap.cached_os_alloc;
        if (os_alloc != NULL) {
            if (os_alloc->size >= size) {
                LTRACEF("Using saved 0x%zx-byte OS alloc @%p (>=0x%zx bytes)\n",
                        os_alloc->size, os_alloc, size);
                ptr = os_alloc;
                size = os_alloc->size;
                DEBUG_ASSERT_MSG(IS_PAGE_ALIGNED(ptr),
                                 "0x%zx bytes @%p", size, ptr);
                DEBUG_ASSERT_MSG(IS_PAGE_ALIGNED(size),
                                 "0x%zx bytes @%p", size, ptr);
            } else {
                // We need to allocate more from the OS. Return the cached OS
                // allocation, in case we're holding an unusually-small block
                // that's unlikely to satisfy future calls to heap_grow().
                LTRACEF("Returning too-small saved 0x%zx-byte OS alloc @%p "
                        "(<0x%zx bytes)\n",
                        os_alloc->size, os_alloc, size);
                free_to_os(os_alloc, os_alloc->size);
            }
            theheap.cached_os_alloc = NULL;
        }
    }
    if (ptr == NULL) {
        ptr = heap_page_alloc(size >> PAGE_SIZE_SHIFT);
        if (ptr == NULL) {
            return MX_ERR_NO_MEMORY;
        }
        LTRACEF("Growing heap by 0x%zx bytes, new ptr %p (%s)\n",
                size, ptr, bucket ? "large" : "small");
    }

    theheap.size += size;
    add_to_heap(ptr, size, bucket);

    return size;
}

void cmpct_init(void) {
    LTRACE_ENTRY;

    // Create a mutex.
    mutex_init(&theheap.lock);

    // Initialize the free list.
    for (int i = 0; i < NUMBER_OF_BUCKETS; i++) {
        theheap.free_lists[i] = NULL;
    }
    for (int i = 0; i < BUCKET_WORDS; i++) {
        theheap.free_list_bits[i] = 0;
    }

    size_t initial_alloc = HEAP_GROW_SIZE - 2 * sizeof(header_t);

    theheap.remaining = 0;

    heap_grow(initial_alloc, NULL);
}
