// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/cmdline.h>
#include <lib/memory_limit.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <kernel/range_check.h>
#include <ktl/algorithm.h>
#include <pretty/sizes.h>
#include <vm/bootreserve.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#define memlim_logf(...)            \
  if (MemoryLimitDbg) {             \
    printf("memlim: " __VA_ARGS__); \
  }

// The max bytes of memory allowed by the system. Since it's specified in MB via the command
// line argument it will always be page aligned.
static size_t SystemMemoryLimit = 0;
// On init this is set to the memory limit and then decremented as we add memory to the system.
static size_t SystemMemoryRemaining = 0;
static bool MemoryLimitDbg = false;

typedef struct reserve_entry {
  uintptr_t start;      // Start of the reserved range
  size_t len;           // Length of the reserved range, does not change as start/end are adjusted
  uintptr_t end;        // End of the reserved range
  size_t unused_front;  // Space before the region that is available
  size_t unused_back;   // Space after the region that is available
} reserve_entry_t;

// Boot reserve entries are processed and added here for memory limit calculations.
const size_t kReservedRegionMax = 64;
static reserve_entry_t ReservedRegions[kReservedRegionMax];
static size_t ReservedRegionCount;

static zx_status_t add_arena(uintptr_t base, size_t size, pmm_arena_info_t arena_template) {
  auto arena = arena_template;
  arena.base = base;
  arena.size = size;
  return pmm_add_arena(&arena);
}

static void print_reserve_state(void) {
  if (!MemoryLimitDbg) {
    return;
  }

  for (size_t i = 0; i < ReservedRegionCount; i++) {
    const auto& entry = ReservedRegions[i];
    printf("%zu: [f: %-#10" PRIxPTR " |%#10" PRIxPTR " - %-#10" PRIxPTR "| (len: %#10" PRIxPTR
           ") b: %-#10" PRIxPTR "]\n",
           i, entry.unused_front, entry.start, entry.end, entry.len, entry.unused_back);
  }
}

zx_status_t memory_limit_init() {
  if (!SystemMemoryLimit) {
    ReservedRegionCount = 0;
    SystemMemoryLimit = gCmdline.GetUInt64("kernel.memory-limit-mb", 0u) * MB;
    if (!SystemMemoryLimit) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // For now, always print debug information if a limit is imposed.
    MemoryLimitDbg = gCmdline.GetBool("kernel.memory-limit-dbg", true);
    SystemMemoryRemaining = SystemMemoryLimit;
    return ZX_OK;
  }

  return ZX_ERR_BAD_STATE;
}

static size_t record_bytes_needed(size_t len) {
  const size_t vm_pages_per_page = PAGE_SIZE / sizeof(vm_page_t);
  // This is how many pages are needed to represent the range. Each needs one vm_page_t.
  size_t pages_cnt = (len + (PAGE_SIZE - 1)) / PAGE_SIZE;
  // We need vm_page_t entries for each page above
  size_t pages_first_level = (pages_cnt + (vm_pages_per_page - 1)) / vm_pages_per_page;
  // We may need a page to do a second level of tracking the first level pages
  size_t pages_second_level = (pages_first_level + (vm_pages_per_page - 1)) / vm_pages_per_page;
  // And finally to support ranges larger than ~8GB we need one more level
  size_t pages_third_level = (pages_second_level + (vm_pages_per_page - 1)) / vm_pages_per_page;
  return PAGE_SIZE * (pages_first_level + pages_second_level + pages_third_level);
}

zx_status_t memory_limit_add_range(uintptr_t range_base, size_t range_size,
                                   pmm_arena_info_t arena_template) {
  // This function is called for every contiguous range of memory the system wants to add
  // to the PMM. Some systems have a simple layout of a single memory range. Other systems
  // may have multiple due to segmentation between < 4 GB and higher, or ranges broken up
  // by peripheral memory and EFI runtime services. To handle these circumstances we walk
  // the list of boot reserved regions entirely for each to check if they exist in the given
  // range added to the system.
  //
  // Arenas passed to us should never overlap. For that reason we can get a good idea of whether
  // a given memory limit can fit all the reserved regions by trying to fulfill their vm_page_t
  // requirements while processing the arenas themselves, rather than waiting until later.
  auto cb = [range_base, range_size](reserve_range_t reserve) {
    // Is this reserved region in the arena?
    uintptr_t in_offset;
    size_t in_len;
    // If there's no intersection then move on to the next reserved boot region.
    if (!GetIntersect(range_base, range_size, reserve.pa, reserve.len, &in_offset, &in_len)) {
      return true;
    }

    auto& entry = ReservedRegions[ReservedRegionCount];
    entry.start = reserve.pa;
    entry.len = reserve.len;
    entry.end = reserve.pa + reserve.len;

    // For the first pass the goal is to ensure we can include all reserved ranges along
    // with enough space for their bookkeeping if we have to trim the arenas down due to
    // memory restrictions.
    if (ReservedRegionCount == 0) {
      entry.unused_front = entry.start - range_base;
      entry.unused_back = 0;
    } else {
      auto& prev = ReservedRegions[ReservedRegionCount - 1];
      // There's no limit to how many memory ranges may be added by the platform so we
      // need to figure out if we're in a new contiguous range, or contiguously next to
      // another reservation so we know where to set our starting point for this section.
      // We can tell which one by seeing which is closest to us: the start of the range
      // being added, or the end of the last reserved space we dealt with.
      uintptr_t start = ktl::max(range_base, prev.end);
      if (start == prev.end) {
        // How much room is between us and the start of the previous entry?
        size_t spare_bytes = (reserve.pa - start);
        size_t bytes_needed = record_bytes_needed(prev.len);

        // If there isn't enough space for the previous region's vm_page_t entries
        // then merge it with this reserved range and try again on this range. This
        // typically happens with regions the bootloader placed near each other
        // due to heap fragmentation before booting the kernel.
        if (bytes_needed > spare_bytes) {
          memlim_logf("prev needs %#zx but only %#zx are available, merging with entry\n",
                      bytes_needed, spare_bytes);
          prev.len = prev.len + spare_bytes + entry.len;
          prev.end = entry.end;
          // Return true early to avoid incrementing the region count
          return true;
        }

        // If we're next to a reserved region and have enough space between for their
        // records we'll adjust their range to include as much as needed and keep the rest
        // for ourselves. This later can be consumed if we are allowed to use more memory.
        memlim_logf("increasing entry at %#zx by %#zx for vm_page_t records.\n", prev.start,
                    bytes_needed);
        prev.len += bytes_needed;
        prev.end += bytes_needed;
        entry.unused_front = (spare_bytes - bytes_needed);
      } else {
        // If this entry is the first in a region it can take everything in
        // front of it.
        entry.unused_front = reserve.pa - start;
      }
    }

    // Increment our number of regions and move to the next, unless we've hit the limit.
    ReservedRegionCount++;
    return (ReservedRegionCount < kReservedRegionMax);
  };

  // Something bad happened if we return false from a callback, so just add the arena outright
  // now to prevent the system from falling over when it tries to wire out the heap.
  if (!boot_reserve_foreach(cb)) {
    add_arena(range_base, range_size, arena_template);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // The last entry still needs to have its record pages accounted for.
  // Additionally, If there's still space between the last reserved region in
  // an arena and the end of the arena then it should be accounted for in that
  // last reserved region.
  if (ReservedRegionCount) {
    // First, account for the space in back of the last entry.
    auto& last = ReservedRegions[ReservedRegionCount - 1];
    if (Intersects(range_base, range_size, last.start, last.end)) {
      last.unused_back = (range_base + range_size) - last.end;
    }

    // Now figure out where we can put the records for this region
    size_t needed_bytes = record_bytes_needed(last.len);
    if (needed_bytes < last.unused_front) {
      last.start -= needed_bytes;
      last.len += needed_bytes;
      last.unused_front -= needed_bytes;
    } else if (needed_bytes < last.unused_back) {
      last.len += needed_bytes;
      last.unused_back -= needed_bytes;
    } else {
      MemoryLimitDbg = true;
      memlim_logf("unable to resolve record pages for final entry!");
      print_reserve_state();
      return ZX_ERR_BAD_STATE;
    }
  }

  memlim_logf("processed arena [%#" PRIxPTR " - %#" PRIxPTR "]\n", range_base,
              range_base + range_size);

  return ZX_OK;
}

zx_status_t memory_limit_add_arenas(pmm_arena_info_t arena_template) {
  memlim_logf("after processing ranges:\n");
  print_reserve_state();

  // First pass, expand to take memory from the front / back of each region as
  // the limit allows.
  for (size_t i = 0; i < ReservedRegionCount; i++) {
    auto& entry = ReservedRegions[i];
    // Now expand based on any remaining memory we have to spare from the front
    // and back of the reserved region.
    size_t available = ktl::min(SystemMemoryRemaining, entry.unused_front);
    if (available) {
      SystemMemoryRemaining -= available;
      entry.unused_front -= available;
      entry.start = PAGE_ALIGN(entry.start - available);
    }

    available = ktl::min(SystemMemoryRemaining, entry.unused_back);
    if (available) {
      SystemMemoryRemaining -= available;
      entry.unused_back -= available;
      entry.end = PAGE_ALIGN(entry.end + available);
    }
  }

  memlim_logf("first pass; %#" PRIxPTR " remaining\n", SystemMemoryRemaining);
  print_reserve_state();
  memlim_logf("second pass; merging arenas\n");

  // Second pass, coalesce the regions into the smallest number of arenas possible
  for (size_t i = 0; i < ReservedRegionCount - 1; i++) {
    auto& cur = ReservedRegions[i];
    auto& next = ReservedRegions[i + 1];

    if (cur.end == next.start) {
      memlim_logf("merging |%#" PRIxPTR " - %#" PRIxPTR "| and |%#" PRIxPTR " - %#" PRIxPTR "|\n",
                  cur.start, cur.end, next.start, next.end);
      cur.end = next.end;
      memmove(&ReservedRegions[i + 1], &ReservedRegions[i + 2],
              sizeof(reserve_entry_t) * (ReservedRegionCount - i - 2));
      // We've removed on entry and we also need to compare this new current entry to the new
      // next entry. To do so, we hold our position in the loop and come around again.
      ReservedRegionCount--;
      i--;
    }
  }

  print_reserve_state();

  // Last pass, add arenas to the system
  for (uint i = 0; i < ReservedRegionCount; i++) {
    auto& entry = ReservedRegions[i];
    size_t size = entry.end - entry.start;
    memlim_logf("adding [%#" PRIxPTR " - %#" PRIxPTR "]\n", entry.start, entry.end);
    zx_status_t status = add_arena(entry.start, size, arena_template);
    if (status != ZX_OK) {
      printf("MemoryLimit: Failed to add arena [%#" PRIxPTR " - %#" PRIxPTR
             "]: %d, system problems may result!\n",
             entry.start, entry.end, status);
    }
  }

  return ZX_OK;
}
