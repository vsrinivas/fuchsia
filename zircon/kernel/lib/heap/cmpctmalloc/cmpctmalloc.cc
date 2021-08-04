// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/cmpctmalloc.h"

#include <inttypes.h>
#ifdef _KERNEL
#include <lib/counters.h>
#endif
#include <lib/heap.h>
#include <lib/heap_internal.h>
#include <lib/zircon-internal/align.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/types.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <pretty/hexdump.h>

#define KERNEL_ASAN (__has_feature(address_sanitizer) && _KERNEL)

#if KERNEL_ASAN
#include <lib/instrumentation/asan.h>
#else  // !KERNEL_ASAN
#define NO_ASAN
#endif  // KERNEL_ASAN

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
//   - FREE_BIT: The area is free, and lives in a free bucket.
//   These bits shouldn't be checked directly; use the is_tagged_as_*()
//   functions.
//
//   If the area is free (is_tagged_as_free(header_t*)), the area's header
//   includes the doubly-linked free list pointers defined by free_t (which is a
//   header_t overlay). Those pointers are used to chain the free area off of
//   the appropriately-sized free bucket.
//
// Normal (small/non-large) allocation:
//   An alloction of less than or equal to (HEAP_LARGE_ALLOC_BYTES - sizeof(header_t)), which
//   can fit in a free bucket.
//
// Large allocation:
//   An alloction of more than (HEAP_LARGE_ALLOC_BYTES - sizeof(header_t)). This
//   is no longer allowed.
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
#include <platform.h>

#define CMPCT_DEBUG
#endif

#ifdef _KERNEL
#include <debug.h>
#include <lib/ktrace.h>
#include <trace.h>

#include <kernel/auto_preempt_disabler.h>

using LocalTraceDuration =
    TraceDuration<TraceEnabled<false>, KTRACE_GRP_SCHEDULER, TraceContext::Thread>;

#define LOCAL_TRACE_DURATION(label, name, ...) \
  LocalTraceDuration name { KTRACE_STRING_REF(label), ##__VA_ARGS__ }

#define LOCAL_TRACE_DURATION_END(name) name.End()

#define PREEMPT_DISABLE(name) AutoPreemptDisabler name

using LockGuard = ::Guard<Mutex>;

KCOUNTER(malloc_size_le_64, "malloc.size_le_64")
KCOUNTER(malloc_size_le_96, "malloc.size_le_96")
KCOUNTER(malloc_size_le_128, "malloc.size_le_128")
KCOUNTER(malloc_size_le_256, "malloc.size_le_256")
KCOUNTER(malloc_size_le_384, "malloc.size_le_384")
KCOUNTER(malloc_size_le_512, "malloc.size_le_512")
KCOUNTER(malloc_size_le_1024, "malloc.size_le_1024")
KCOUNTER(malloc_size_le_2048, "malloc.size_le_2048")
KCOUNTER(malloc_size_other, "malloc.size_other")
// The number of failed attempts at growing the heap.
KCOUNTER(malloc_heap_grow_fail, "malloc.heap_grow_fail")

#else

// When built in a host (aka non-kernel) environment it is assumed to be for testing and so we want
// to make sure that zircon assertions are correctly enabled. This to prevent any regressions due to
// libraries and build system changes.
static_assert(ZX_DEBUG_ASSERT_IMPLEMENTED, "Expect debug assertions in host builds");

#define LOCAL_TRACE_DURATION(label, name, ...)
#define LOCAL_TRACE_DURATION_END(name)
#define PREEMPT_DISABLE(name)

class __TA_SCOPED_CAPABILITY LockGuard {
 public:
  LockGuard(std::mutex* lock) __TA_ACQUIRE(lock) : guard_(*lock) {}
  ~LockGuard() __TA_RELEASE() = default;

  void Release() __TA_RELEASE() { guard_.unlock(); }

 private:
  std::unique_lock<std::mutex> guard_;
};

#define LTRACEF(...)
#define LTRACE_ENTRY
#define INFO 1

#endif

#define LOCAL_TRACE 0

// Use HEAP_ENABLE_TESTS to enable internal testing. The tests are not useful
// when the target system is up. By that time we have done hundreds of allocations
// already.

#define ALLOC_FILL 0x99
#define FREE_FILL 0x77
#define PADDING_FILL 0x55

#if !defined(HEAP_GROW_SIZE)
// HEAP_GROW_SIZE is minimum size by which the heap is grown.
//
// A larger value can provide some performance improvement at the cost of wasted
// memory.
//
// See also |HEAP_LARGE_ALLOC_BYTES|.
#define HEAP_GROW_SIZE size_t(256 * 1024)
#endif

static_assert(ZX_IS_PAGE_ALIGNED(HEAP_GROW_SIZE), "");

// HEAP_ALLOC_VIRTUAL_BITS defines the largest allocation bucket.
//
// The requirements on virtual bits is that the largest allocation (including
// header), must roundup to not more 2**HEAP_ALLOC_VIRTUAL_BITS than this
// alignment, and similarly the heap cannot grow by amounts that would not round
// down to 2**HEAP_ALLOC_VIRTUAL_BITS or less. As such the heap can grow by more
// than this many bits at once, but not so many as it must fall into the next
// bucket.
#define HEAP_ALLOC_VIRTUAL_BITS 21

// HEAP_LARGE_ALLOC_BYTES limits size of any single allocation.
//
// A larger value will, on average, "waste" more memory. Why is that? When
// freeing memory the heap may hold on to a block before returning it to the
// underlying allocator (see |theheap.cached_os_alloc|). The size of the cached
// block is limited by HEAP_LARGE_ALLOC_BYTES so reducing this value limits the
// size of the cached block.
//
// Note that HEAP_LARGE_ALLOC_BYTES is the largest internal allocation that the
// heap can do, and includes any headers. The largest allocation cmpct_alloc
// could theoretically (it may be artificially limited) provide is therefore
// slightly less than this.
//
// See also |HEAP_GROW_SIZE|.
#define HEAP_LARGE_ALLOC_BYTES ((size_t(1) << HEAP_ALLOC_VIRTUAL_BITS) - kHeapGrowOverhead)

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

#define HEADER_LEFT_BIT_MASK (FREE_BIT)

// All individual memory areas on the heap start with this.
typedef struct header_struct {
  // Pointer to the previous area in memory order. The lower bit is used
  // to store extra state: see FREE_BIT. The left sentinel will have
  // NULL in the address portion of this field. Left and right sentinels
  // will always be marked as "allocated" to avoid coalescing.
  struct header_struct* left;
  // The size of the memory area in bytes, including this header.
  // The right sentinel will have 0 in this field.
  size_t size;
} header_t;

// When the heap is grown the requested internal usable size will be increased
// by this amount before allocating from the OS. This can be factored into
// any heap_grow requested to precisely control the OS allocation amount.
constexpr size_t kHeapGrowOverhead = sizeof(header_t) * 2;

// Precalculated version of HEAP_GROW_SIZE that takes into account the grow
// overhead.
constexpr size_t kHeapUsableGrowSize = HEAP_GROW_SIZE - kHeapGrowOverhead;

// When we grow the heap we have to have somewhere in the freelist to put the
// resulting freelist entry, so the freelist has to have a certain number of
// buckets.
static_assert(HEAP_GROW_SIZE <= HEAP_LARGE_ALLOC_BYTES);

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

  // Free lists, bucketed by size. See size_to_index_helper().
  free_t* free_lists[NUMBER_OF_BUCKETS];

  // Bitmask that tracks whether a given free_lists entry has any elements.
  // See set_free_list_bit(), clear_free_list_bit().
#define BUCKET_WORDS (((NUMBER_OF_BUCKETS) + 31) >> 5)
  uint32_t free_list_bits[BUCKET_WORDS];

#if __has_feature(address_sanitizer) && _KERNEL
  asan::Quarantine asan_quarantine;
#endif
};

// Heap static vars.
static struct heap theheap TA_GUARDED(TheHeapLock::Get());

NO_ASAN static void dump_free(header_t* header) TA_REQ(TheHeapLock::Get()) {
  // This function accesses header->size so it has to be NO_ASAN
  dprintf(INFO, "\t\tbase %p, end %#" PRIxPTR ", len %#zx (%zu)\n", header,
          (uintptr_t)header + header->size, header->size, header->size);
}

NO_ASAN static void cmpct_dump_locked() TA_REQ(TheHeapLock::Get()) {
  // This function accesses free_t * that are poisoned so it has to be NO_ASAN
  dprintf(INFO, "Heap dump (using cmpctmalloc):\n");
  dprintf(INFO, "\tsize %lu, remaining %lu, cached free %lu\n", (unsigned long)theheap.size,
          (unsigned long)theheap.remaining,
          theheap.cached_os_alloc ? theheap.cached_os_alloc->size : 0);

  dprintf(INFO, "\tfree list:\n");
  for (int i = 0; i < NUMBER_OF_BUCKETS; i++) {
    bool header_printed = false;
    free_t* free_area = theheap.free_lists[i];
    for (; free_area != NULL; free_area = free_area->next) {
      ZX_ASSERT(free_area != free_area->next);
      if (!header_printed) {
        dprintf(INFO, "\tbucket %d\n", i);
        header_printed = true;
      }
      dump_free(&free_area->header);
    }
  }
}

struct SizeToIndexRet {
  int bucket;
  size_t rounded_up;
};

// Operates in sizes that don't include the allocation header;
// i.e., the usable portion of a memory area.
static constexpr SizeToIndexRet SizeToIndexHelper(size_t size, int adjust, int increment) {
  size_t rounded_up = 0;
  // First buckets are simply 8-spaced up to 128.
  if (size <= 128) {
    if (sizeof(size_t) == 8u && size <= sizeof(free_t) - sizeof(header_t)) {
      rounded_up = sizeof(free_t) - sizeof(header_t);
    } else {
      rounded_up = size;
    }
    // No allocation is smaller than 8 bytes, so the first bucket is for 8
    // byte spaces (not including the header).  For 64 bit, the free list
    // struct is 16 bytes larger than the header, so no allocation can be
    // smaller than that (otherwise how to free it), but we have empty 8
    // and 16 byte buckets for simplicity.
    return {(int)((size >> 3) - 1), rounded_up};
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
  unsigned row = (unsigned)(sizeof(size_t) * 8 - 4 - __builtin_clzl(size));
  // For row 4 we want to shift down 4 bits.
  unsigned column = (size >> row) & 7;
  int row_column = (row << 3) | column;
  row_column += increment;
  size = (8 + (row_column & 7)) << (row_column >> 3);
  rounded_up = size;
  // We start with 15 buckets, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96,
  // 104, 112, 120.  Then we have row 4, sizes 128 and up, with the
  // row-column 8 and up.
  int answer = row_column + 15 - 32;
  ZX_DEBUG_ASSERT(answer < NUMBER_OF_BUCKETS);
  return {answer, rounded_up};
}

// Round up size to next bucket when allocating.
static constexpr SizeToIndexRet SizeToIndexAllocating(size_t size) {
  size_t rounded = ZX_ROUNDUP(size, 8);
  return SizeToIndexHelper(rounded, -8, 1);
}

// Round down size to next bucket when freeing.
static constexpr int size_to_index_freeing(size_t size) {
  return SizeToIndexHelper(size, 0, 0).bucket;
}

// Ensure that HEAP_LARGE_ALLOC_BYTES maps to a valid bucket when allocating.
// Given how HEAP_LARGE_ALLOC_BYTES is defined this assert is somewhat excessive
// but included for completeness should HEAP_LARGE_ALLOC_BYTES ever be given a
// more complicated definition. Note that HEAP_LARGE_ALLOC_BYTES is the internal
// size (including header), but we map to a bucket using the size without the
// header, as the header is implicitly included in the final bucket. Or put
// another way, the bucket for size X gives a bucket with a free block of
// X+sizeof(header_t).
static_assert(SizeToIndexAllocating(HEAP_LARGE_ALLOC_BYTES - sizeof(header_t)).bucket <=
              NUMBER_OF_BUCKETS);

static int size_to_index_allocating(size_t size, size_t* rounded_up_out) {
  auto result = SizeToIndexAllocating(size);
  *rounded_up_out = result.rounded_up;
  return result.bucket;
}

static inline header_t* tag_as_free(void* left) TA_REQ(TheHeapLock::Get()) {
  return (header_t*)((uintptr_t)left | FREE_BIT);
}

// Returns true if this header_t is marked as free.
NO_ASAN static inline bool is_tagged_as_free(const header_t* header) TA_REQ(TheHeapLock::Get()) {
  // The free bit is stashed in the lower bit of header->left.
  return ((uintptr_t)(header->left) & FREE_BIT) != 0;
}

static inline header_t* untag(const void* left) TA_REQ(TheHeapLock::Get()) {
  return (header_t*)((uintptr_t)left & ~HEADER_LEFT_BIT_MASK);
}

NO_ASAN static inline header_t* right_header(header_t* header) TA_REQ(TheHeapLock::Get()) {
  return (header_t*)((char*)header + header->size);
}

static inline void set_free_list_bit(int index) TA_REQ(TheHeapLock::Get()) {
  theheap.free_list_bits[index >> 5] |= (1u << (31 - (index & 0x1f)));
}

static inline void clear_free_list_bit(int index) TA_REQ(TheHeapLock::Get()) {
  theheap.free_list_bits[index >> 5] &= ~(1u << (31 - (index & 0x1f)));
}

static int find_nonempty_bucket(int index) TA_REQ(TheHeapLock::Get()) {
  uint32_t mask = (1u << (31 - (index & 0x1f))) - 1;
  mask = mask * 2 + 1;
  mask &= theheap.free_list_bits[index >> 5];
  if (mask != 0) {
    return (index & ~0x1f) + __builtin_clz(mask);
  }
  for (index = ZX_ROUNDUP(index + 1, 32); index < NUMBER_OF_BUCKETS; index += 32) {
    mask = theheap.free_list_bits[index >> 5];
    if (mask != 0u) {
      return index + __builtin_clz(mask);
    }
  }
  return -1;
}

NO_ASAN static bool is_start_of_os_allocation(const header_t* header) TA_REQ(TheHeapLock::Get()) {
  return untag(header->left) == untag(NULL);
}

NO_ASAN static void create_free_area(void* address, void* left, size_t size)
    TA_REQ(TheHeapLock::Get()) {
  free_t* free_area = (free_t*)address;
  free_area->header.size = size;
  free_area->header.left = tag_as_free(left);

  int index = size_to_index_freeing(size - sizeof(header_t));
  set_free_list_bit(index);
  free_t** bucket = &theheap.free_lists[index];

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

NO_ASAN static bool is_end_of_os_allocation(char* address) TA_REQ(TheHeapLock::Get()) {
  return ((header_t*)address)->size == 0;
}

NO_ASAN static void free_to_os(void* ptr, size_t size) TA_REQ(TheHeapLock::Get()) {
  ZX_DEBUG_ASSERT(ZX_IS_PAGE_ALIGNED(ptr));
  ZX_DEBUG_ASSERT(ZX_IS_PAGE_ALIGNED(size));
  heap_page_free(ptr, size >> ZX_PAGE_SHIFT);
  theheap.size -= size;
}

// May call free_to_os(), or may cache the (non-large) OS allocation in
// cached_os_alloc. |left_sentinel| is the start of the OS allocation, and
// |total_size| is the (page-aligned) number of bytes that were originally
// allocated from the OS.
NO_ASAN static void possibly_free_to_os(header_t* left_sentinel, size_t total_size)
    TA_REQ(TheHeapLock::Get()) {
  if (theheap.cached_os_alloc == NULL) {
    LTRACEF("Keeping 0x%zx-byte OS alloc @%p\n", total_size, left_sentinel);
    theheap.cached_os_alloc = left_sentinel;
    theheap.cached_os_alloc->left = NULL;
    theheap.cached_os_alloc->size = total_size;
  } else {
    LTRACEF("Returning 0x%zx bytes @%p to OS\n", total_size, left_sentinel);
    free_to_os(left_sentinel, total_size);
  }
}

// Frees |size| bytes starting at |address|, either to a free bucket or to the
// OS (in which case the left/right sentinels are freed as well). |address|
// should point to what would be the header_t of the memory area to free, and
// |left| and |size| should be set to the values that the header_t would have
// contained. This is broken out because the header_t will not contain the
// proper size when coalescing neighboring areas.
NO_ASAN static void free_memory(void* address, void* left, size_t size) TA_REQ(TheHeapLock::Get()) {
  left = untag(left);
  if (ZX_IS_PAGE_ALIGNED(left) && is_start_of_os_allocation((const header_t*)left) &&
      is_end_of_os_allocation((char*)address + size)) {
    // Assert that it's safe to do a simple 2*sizeof(header_t)) below.
    ZX_DEBUG_ASSERT_MSG(((header_t*)left)->size == sizeof(header_t),
                        "Unexpected left sentinel size %zu != header size %zu",
                        ((header_t*)left)->size, sizeof(header_t));
    possibly_free_to_os((header_t*)left, size + 2 * sizeof(header_t));
  } else {
    create_free_area(address, left, size);
  }
}

NO_ASAN static void unlink_free(free_t* free_area, int bucket) TA_REQ(TheHeapLock::Get()) {
  ZX_ASSERT_MSG(theheap.remaining >= free_area->header.size, "%zu >= %zu\n", theheap.remaining,
                free_area->header.size);
  theheap.remaining -= free_area->header.size;
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

NO_ASAN static void unlink_free_unknown_bucket(free_t* free_area) TA_REQ(TheHeapLock::Get()) {
  return unlink_free(free_area, size_to_index_freeing(free_area->header.size - sizeof(header_t)));
}

NO_ASAN static void* create_allocation_header(void* address, size_t offset, size_t size, void* left)
    TA_REQ(TheHeapLock::Get()) {
  header_t* standalone = (header_t*)((char*)address + offset);
  standalone->left = untag(left);
  standalone->size = size;
  return standalone + 1;
}

NO_ASAN static void FixLeftPointer(header_t* right, header_t* new_left) TA_REQ(TheHeapLock::Get()) {
  int tag = (uintptr_t)right->left & 1;
  right->left = (header_t*)(((uintptr_t)new_left & ~1) | tag);
}

#ifdef CMPCT_DEBUG
[[maybe_unused]] NO_ASAN static void check_free_fill(void* ptr, size_t size)
    TA_REQ(TheHeapLock::Get()) {
  // The first 16 bytes of the region won't have free fill due to overlap
  // with the allocator bookkeeping.
  const size_t start = sizeof(free_t) - sizeof(header_t);
  for (size_t i = start; i < size; ++i) {
    uint8_t byte = ((uint8_t*)ptr)[i];
    if (byte != FREE_FILL) {
      platform_panic_start();
      printf("Heap free fill check fail.  Allocated region:\n");
      hexdump8(ptr, size);
      panic("allocating %lu bytes, fill was %02x, offset %lu\n", size, byte, i);
    }
  }
}
#endif

static void add_to_heap(void* new_area, size_t size) TA_REQ(TheHeapLock::Get()) {
  char* top = (char*)new_area + size;
  // Set up the left sentinel. Its |left| field will not have FREE_BIT set,
  // stopping attempts to coalesce left.
  header_t* left_sentinel = (header_t*)new_area;
  create_allocation_header(left_sentinel, 0, sizeof(header_t), NULL);

  // Set up the usable memory area, which will be marked free.
  header_t* new_header = left_sentinel + 1;
  size_t free_size = size - 2 * sizeof(header_t);
  create_free_area(new_header, left_sentinel, free_size);

  // Set up the right sentinel. Its |left| field will not have FREE_BIT bit
  // set, stopping attempts to coalesce right.
  header_t* right_sentinel = (header_t*)(top - sizeof(header_t));
  create_allocation_header(right_sentinel, 0, 0, new_header);
}

// Create a new free-list entry of at least size bytes (including the
// allocation header).  Called with the lock, apart from during init.
NO_ASAN static ssize_t heap_grow(size_t size) TA_REQ(TheHeapLock::Get()) {
  // This function accesses field members of header_t which are poisoned so it
  // has to be NO_ASAN.

  // We expect to never have been asked to grow by more than the maximum
  // allocation
  ZX_DEBUG_ASSERT(size <= HEAP_LARGE_ALLOC_BYTES);

  // Ensure that after performing the size manipulations below we do not end up
  // overflowing the maximum bucket. This check is useful since an obvious
  // setting of HEAP_LARGE_ALLOC_BYTES == 1<<HEAP_ALLOC_VIRTUAL_BITS will
  // actually result in us growing the heap by *more* than 1<<HEAP_ALLOC_VIRTUAL_BITS
  // However this is typically safe since growing by an extra partial page should
  // not send us into the next bucket. However if pages are large enough and
  // HEAP_ALLOC_VIRTUAL_BITS small enough this could happen, and so this assert
  // exists to prevent choosing such sizes.
  static_assert(
      size_to_index_freeing(ZX_ROUNDUP(HEAP_LARGE_ALLOC_BYTES + kHeapGrowOverhead, ZX_PAGE_SIZE)) -
          kHeapGrowOverhead - sizeof(header_t) <=
      NUMBER_OF_BUCKETS);

  // The new free list entry will have a header on each side (the
  // sentinels) so we need to grow the gross heap size by this much more.
  size += kHeapGrowOverhead;
  size = ZX_ROUNDUP(size, ZX_PAGE_SIZE);

  void* ptr = NULL;

  header_t* os_alloc = (header_t*)theheap.cached_os_alloc;
  if (os_alloc != NULL) {
    if (os_alloc->size >= size) {
      LTRACEF("Using saved 0x%zx-byte OS alloc @%p (>=0x%zx bytes)\n", os_alloc->size, os_alloc,
              size);
      ptr = os_alloc;
      size = os_alloc->size;
      ZX_DEBUG_ASSERT_MSG(ZX_IS_PAGE_ALIGNED(ptr), "0x%zx bytes @%p", size, ptr);
      ZX_DEBUG_ASSERT_MSG(ZX_IS_PAGE_ALIGNED(size), "0x%zx bytes @%p", size, ptr);
    } else {
      // We need to allocate more from the OS. Return the cached OS
      // allocation, in case we're holding an unusually-small block
      // that's unlikely to satisfy future calls to heap_grow().
      LTRACEF(
          "Returning too-small saved 0x%zx-byte OS alloc @%p "
          "(<0x%zx bytes)\n",
          os_alloc->size, os_alloc, size);
      free_to_os(os_alloc, os_alloc->size);
    }
    theheap.cached_os_alloc = NULL;
  }
  if (ptr == NULL) {
    ptr = heap_page_alloc(size >> ZX_PAGE_SHIFT);
    if (ptr == NULL) {
#ifdef _KERNEL
      kcounter_add(malloc_heap_grow_fail, 1);
#endif
      return ZX_ERR_NO_MEMORY;
    }
    LTRACEF("Growing heap by 0x%zx bytes, new ptr %p\n", size, ptr);
    theheap.size += size;
  }

  add_to_heap(ptr, size);

  return size;
}

// Use HEAP_ENABLE_TESTS to enable internal testing. The tests are not useful
// when the target system is up. By that time we have done hundreds of allocations
// already.
#ifdef HEAP_ENABLE_TESTS

static inline size_t cmpct_heap_remaining() TA_EXCL(TheHeapLock::Get()) {
  LockGuard guard(TheHeapLock::Get());
  return theheap.remaining;
}

static void WasteFreeMemory(void) TA_EXCL(TheHeapLock::Get()) {
  while (cmpct_heap_remaining() != 0) {
    cmpct_alloc(1);
  }
}

// If we just make a big allocation it gets rounded off.  If we actually
// want to use a reasonably accurate amount of memory for test purposes, we
// have to do many small allocations.
static void* TestTrimHelper(ssize_t target) TA_EXCL(TheHeapLock::Get()) {
  void* answer = NULL;
  size_t remaining = cmpct_heap_remaining();
  while (cmpct_heap_remaining() - target > 512) {
    void* next_block =
        static_cast<char*>(cmpct_alloc(8 + ((cmpct_heap_remaining() - target) >> 2)));
    *(static_cast<void**>(next_block)) = answer;
    answer = next_block;
    if (cmpct_heap_remaining() > remaining) {
      return answer;
    }
    // Abandon attempt to hit particular freelist entry size if we
    // accidentally got more memory from the OS.
    remaining = cmpct_heap_remaining();
  }
  return answer;
}

static void TestTrimFreeHelper(char* block) TA_EXCL(TheHeapLock::Get()) {
  while (block) {
    char* next_block = *(char**)block;
    cmpct_free(block);
    block = next_block;
  }
}

static void cmpct_test_buckets(void) TA_EXCL(TheHeapLock::Get()) {
  size_t rounded;
  unsigned bucket;
  // Check for the 8-spaced buckets up to 128.
  for (unsigned i = 1; i <= 128; i++) {
    // Round up when allocating.
    bucket = size_to_index_allocating(i, &rounded);
    unsigned expected = (ZX_ROUNDUP(i, 8) >> 3) - 1;
    ZX_ASSERT(bucket == expected);
    ZX_ASSERT(ZX_IS_ALIGNED(rounded, 8));
    ZX_ASSERT(rounded >= i);
    if (i >= sizeof(free_t) - sizeof(header_t)) {
      // Once we get above the size of the free area struct (4 words), we
      // won't round up much for these small size.
      ZX_ASSERT(rounded - i < 8);
    }
    // Only rounded sizes are freed.
    if ((i & 7) == 0) {
      // Up to size 128 we have exact buckets for each multiple of 8.
      ZX_ASSERT(bucket == (unsigned)size_to_index_freeing(i));
    }
  }
  int bucket_base = 7;
  for (unsigned j = 16; j < 1024; j *= 2, bucket_base += 8) {
    // Note the "<=", which ensures that we test the powers of 2 twice to
    // ensure that both ways of calculating the bucket number match.
    for (unsigned i = j * 8; i <= j * 16; i++) {
      // Round up to j multiple in this range when allocating.
      bucket = size_to_index_allocating(i, &rounded);
      unsigned expected = bucket_base + ZX_ROUNDUP(i, j) / j;
      ZX_ASSERT(bucket == expected);
      ZX_ASSERT(ZX_IS_ALIGNED(rounded, j));
      ZX_ASSERT(rounded >= i);
      ZX_ASSERT(rounded - i < j);
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
          ZX_ASSERT((int)bucket == size_to_index_freeing(i));
        } else {
          ZX_ASSERT((int)bucket - 1 == size_to_index_freeing(i));
        }
      }
    }
  }
}

static void cmpct_test_get_back_newly_freed_helper(size_t size) TA_EXCL(TheHeapLock::Get()) {
  void* allocated = cmpct_alloc(size);
  if (allocated == NULL) {
    return;
  }
  char* allocated2 = static_cast<char*>(cmpct_alloc(8));
  char* expected_position = (char*)allocated + size;
  if (allocated2 < expected_position || allocated2 > expected_position + 128) {
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
  ZX_ASSERT(allocated3 == allocated);
  cmpct_free(allocated2);
  cmpct_free(allocated3);
}

static void cmpct_test_get_back_newly_freed(void) TA_EXCL(TheHeapLock::Get()) {
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

static void cmpct_test_return_to_os(void) TA_EXCL(TheHeapLock::Get()) {
  size_t remaining = cmpct_heap_remaining();
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
  if (((uintptr_t)a & (ZX_PAGE_SIZE - 1)) != sizeof(header_t) * 2) {
    return;
  }
  // No trim needed when the entire OS allocation is free.
  ZX_ASSERT(remaining == cmpct_heap_remaining());
}
#endif  // HEAP_ENABLE_TESTS

/****************************************************
 *
 * Public API
 *
 ****************************************************/

// Factors in the header for an allocation. Value chosen here is hard coded and could be less than
// the actual largest allocation that cmpct_alloc could provide. This is done so that larger buckets
// can exist in order to allow the heap to grow by amounts larger than what we would like to allow
// clients to allocate.
constexpr size_t kHeapMaxAllocSize = (size_t(1) << 20) - sizeof(header_t);

// Ensure that the maximum allocation is actually satisfiable. Note that since
// HEAP_LARGE_ALLOC_BYTES is an internal allocation limit we have to add the header on. We have
// already checked previously that the HEAP_LARGE_ALLOC_BYTES is a valid amount to grow the heap by.
static_assert(SizeToIndexAllocating(kHeapMaxAllocSize).rounded_up + sizeof(header_t) <=
              HEAP_LARGE_ALLOC_BYTES);

NO_ASAN void* cmpct_alloc(size_t size) {
  LOCAL_TRACE_DURATION("cmpct_alloc", trace, size, 0);
  if (size == 0u) {
    return NULL;
  }

#ifdef _KERNEL
  if (size <= 64) {
    kcounter_add(malloc_size_le_64, 1);
  } else if (size <= 96) {
    kcounter_add(malloc_size_le_96, 1);
  } else if (size <= 128) {
    kcounter_add(malloc_size_le_128, 1);
  } else if (size <= 256) {
    kcounter_add(malloc_size_le_256, 1);
  } else if (size <= 384) {
    kcounter_add(malloc_size_le_384, 1);
  } else if (size <= 512) {
    kcounter_add(malloc_size_le_512, 1);
  } else if (size <= 1024) {
    kcounter_add(malloc_size_le_1024, 1);
  } else if (size <= 2048) {
    kcounter_add(malloc_size_le_2048, 1);
  } else {
    kcounter_add(malloc_size_other, 1);
  }
#endif
  // Large allocations are no longer allowed. See fxbug.dev/31229 for details.
  if (size > kHeapMaxAllocSize) {
    return NULL;
  }

#if KERNEL_ASAN
  // Add space at the end of the allocation for a redzone.
  // A redzone is used to detect buffer overflows by oversizing the buffer and poisoning the
  // excess memory. The redzone is after the buffer - before the buffer is a header_t, which
  // is also poisoned.
  const size_t alloc_size = size;
  size += asan_heap_redzone_size(alloc_size);
  // When we validated the max allocation size above, we did not take into account the asan redzone.
  // Unfortunately this cannot presently be checked statically due to the asan_heap_redzone_size not
  // being a constexpr, and so we check it here instead.
  ZX_ASSERT(SizeToIndexAllocating(asan_heap_redzone_size(kHeapMaxAllocSize) + kHeapMaxAllocSize)
                    .rounded_up +
                sizeof(header_t) <=
            HEAP_LARGE_ALLOC_BYTES);
#endif  // KERNEL_ASAN

  size_t rounded_up;
  int start_bucket = size_to_index_allocating(size, &rounded_up);

  rounded_up += sizeof(header_t);

  PREEMPT_DISABLE(preempt_disable);
  LockGuard guard(TheHeapLock::Get());
  LOCAL_TRACE_DURATION("locked", trace_lock);
  int bucket = find_nonempty_bucket(start_bucket);
  if (bucket == -1) {
    // Grow heap by at least 12% if we can.
    size_t growby =
        std::min(HEAP_LARGE_ALLOC_BYTES,
                 std::max(theheap.size >> 3, std::max(kHeapUsableGrowSize, rounded_up)));
    // Validate that our growby calculation is correct, and that if we grew the heap by this amount
    // we would actually satisfy our allocation.
    ZX_DEBUG_ASSERT(growby >= rounded_up);
    // Try to add a new OS allocation to the heap, reducing the size until
    // we succeed or get too small.
    while (heap_grow(growby) < 0) {
      if (growby <= rounded_up) {
        return NULL;
      }
      growby = std::max(growby >> 1, rounded_up);
    }
    bucket = find_nonempty_bucket(start_bucket);
    // It should be the case that, since we hold the heap lock, after growing the heap there should
    // be something in our target bucket. However, if there was any confusion in calculating the
    // |growby| amount, then it's possible we still do not have something. As this could only happen
    // due to a systemic configuration error, and this should get caught in tests, this only needs
    // to be a DEBUG_ASSERT and not a always enabled ASSERT. Further, it should not be possible for
    // the assertion of the growby amount above to succeed and then this assertion to fail.
    ZX_DEBUG_ASSERT(bucket != -1);
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
    create_free_area(free, head, left_over);
    FixLeftPointer(right, (header_t*)free);
    head->header.size -= left_over;
  } else {
    unlink_free(head, bucket);
  }
  void* result = create_allocation_header(head, 0, head->header.size, head->header.left);
#ifdef CMPCT_DEBUG
  check_free_fill(result, size);
  memset(result, ALLOC_FILL, size);
  memset(((char*)result) + size, PADDING_FILL, rounded_up - size - sizeof(header_t));
#endif
#if KERNEL_ASAN
  const uintptr_t redzone_start = reinterpret_cast<uintptr_t>(result) + alloc_size;

  asan_poison_shadow(reinterpret_cast<uintptr_t>(head), sizeof(header_t),
                     kAsanHeapLeftRedzoneMagic);
  asan_poison_shadow(redzone_start, asan_heap_redzone_size(alloc_size), kAsanHeapLeftRedzoneMagic);
  asan_unpoison_shadow(reinterpret_cast<uintptr_t>(result), alloc_size);
#endif  //  KERNEL_ASAN

  return result;
}

NO_ASAN void cmpct_free_internal(void* payload, header_t* header) TA_REQ(TheHeapLock::Get()) {
  ZX_DEBUG_ASSERT(!is_tagged_as_free(header));  // Double free!
  ZX_ASSERT_MSG(header->size > sizeof(header_t), "got %lu min %lu", header->size, sizeof(header_t));

#if KERNEL_ASAN
  asan_poison_shadow(reinterpret_cast<uintptr_t>(payload), header->size - sizeof(header_t),
                     kAsanHeapFreeMagic);
  header = static_cast<header_t*>(theheap.asan_quarantine.push(header));
  if (!header) {
    return;
  }
#endif  // KERNEL_ASAN

  size_t size = header->size;
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
}

NO_ASAN void cmpct_free(void* payload) {
  LOCAL_TRACE_DURATION("cmpct_free", trace);
  if (payload == NULL) {
    return;
  }

  PREEMPT_DISABLE(preempt_disable);
  LockGuard guard(TheHeapLock::Get());
  LOCAL_TRACE_DURATION("locked", trace_locked);
  header_t* header = (header_t*)payload - 1;
  return cmpct_free_internal(payload, header);
}

NO_ASAN void cmpct_sized_free(void* payload, size_t s) {
  LOCAL_TRACE_DURATION("cmpct_free", trace);
  if (payload == NULL) {
    return;
  }

  PREEMPT_DISABLE(preempt_disable);
  LockGuard guard(TheHeapLock::Get());
  LOCAL_TRACE_DURATION("locked", trace_locked);
  header_t* header = (header_t*)payload - 1;
  // header->size is the size of the heap block |payload| is in, plus sizeof(header_t), plus
  // the difference between the block size and the requested allocation size. If kernel ASAN
  // is enabled, it also includes an ASAN redzone.
  ZX_ASSERT_MSG(header->size >= s, "expected %lu got %lu", header->size, s);
#if !KERNEL_ASAN
  // Heap blocks are larger than |s| by at most:
  // 1. sizeof(header_t)
  // 2. sizeof(free_t) - we don't split heap blocks if the remaining space is < free_t,
  //    so free_t additional bytes can be present
  // 3. A bucket- and size-dependent extra space, see cmpct_alloc's computation.
  //
  // The computation here is a conservative limit on that difference rather than a precise limit.
  const size_t max_diff = sizeof(header_t) + sizeof(free_t) + (s >> 2);
  ZX_ASSERT_MSG((header->size - s) <= max_diff, "header->size %lu s %lu", header->size, s);
#endif
  return cmpct_free_internal(payload, header);
}

NO_ASAN void* cmpct_memalign(size_t alignment, size_t size) {
  LOCAL_TRACE_DURATION("cmpct_memalign", trace, alignment, size);
  if (size == 0u) {
    return NULL;
  }

  if (alignment < 8) {
    return cmpct_alloc(size);
  }

  size_t padded_size = size + alignment + sizeof(free_t);

  char* unaligned = (char*)cmpct_alloc(padded_size);
  if (unaligned == NULL) {
    return NULL;
  }

  PREEMPT_DISABLE(preempt_disable);
  LockGuard guard(TheHeapLock::Get());
  LOCAL_TRACE_DURATION("locked", trace_lock);
#if KERNEL_ASAN
  // TODO(fxbug.dev/30033): Separately poison padding and the post-buffer redzone.
  asan_poison_shadow(reinterpret_cast<uintptr_t>(unaligned), padded_size,
                     kAsanHeapLeftRedzoneMagic);
#endif  // KERNEL_ASAN

  size_t mask = alignment - 1;
  uintptr_t payload_int = (uintptr_t)unaligned + sizeof(free_t) + mask;
  char* payload = (char*)(payload_int & ~mask);
  if (unaligned != payload) {
    header_t* unaligned_header = (header_t*)unaligned - 1;
    header_t* header = (header_t*)payload - 1;
    size_t left_over = payload - unaligned;
    create_allocation_header(header, 0, unaligned_header->size - left_over, unaligned_header);
    header_t* right = right_header(unaligned_header);
    unaligned_header->size = left_over;
    FixLeftPointer(right, header);
    LOCAL_TRACE_DURATION_END(trace_lock);
    guard.Release();
    cmpct_free(unaligned);
  }

  // TODO: Free the part after the aligned allocation.
#if KERNEL_ASAN
  asan_unpoison_shadow(reinterpret_cast<uintptr_t>(payload), size);
#endif  // KERNEL_ASAN
  return payload;
}

void cmpct_init(void) {
  LTRACE_ENTRY;
  LockGuard guard(TheHeapLock::Get());

  // Initialize the free lists.
  for (int i = 0; i < NUMBER_OF_BUCKETS; i++) {
    theheap.free_lists[i] = NULL;
  }
  for (int i = 0; i < BUCKET_WORDS; i++) {
    theheap.free_list_bits[i] = 0;
  }

  theheap.size = 0;
  theheap.remaining = 0;
  theheap.cached_os_alloc = NULL;

  heap_grow(kHeapUsableGrowSize);
}

void cmpct_dump(bool panic_time) {
  if (panic_time) {
    // If we are panic'ing, just skip the lock.  All bets are off anyway.
    ([]() TA_NO_THREAD_SAFETY_ANALYSIS { cmpct_dump_locked(); })();
  } else {
    LockGuard guard(TheHeapLock::Get());
    cmpct_dump_locked();
  }
}

NO_ASAN void cmpct_get_info(size_t* used_bytes, size_t* free_bytes, size_t* cached_bytes) {
  LockGuard guard(TheHeapLock::Get());
  if (used_bytes) {
    *used_bytes = theheap.size;
  }
  if (free_bytes) {
    *free_bytes = theheap.remaining;
  }
  if (cached_bytes) {
    *cached_bytes = 0;
    if (theheap.cached_os_alloc) {
      *cached_bytes = theheap.cached_os_alloc->size;
    }
  }
}

#ifdef HEAP_ENABLE_TESTS
void cmpct_test(void) {
  cmpct_test_buckets();
  cmpct_test_get_back_newly_freed();
  cmpct_test_return_to_os();
  cmpct_dump(false);
  void* ptr[16];

  ptr[0] = cmpct_alloc(8);
  ptr[1] = cmpct_alloc(32);
  ptr[2] = cmpct_alloc(7);
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
    ptr[index] = cmpct_memalign(align, (unsigned int)rand() % 32768);
    // printf("ptr[0x%x] = %p, align 0x%x\n", index, ptr[index], align);

    ZX_DEBUG_ASSERT(((uintptr_t)ptr[index] % align) == 0);
    // cmpct_dump(false);
  }

  for (i = 0; i < 16; i++) {
    if (ptr[i]) {
      cmpct_free(ptr[i]);
    }
  }

  cmpct_dump(false);
}

#else
void cmpct_test(void) {}
#endif  // HEAP_ENABLE_TESTS
