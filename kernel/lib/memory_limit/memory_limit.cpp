// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memory_limit.h>

#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <inttypes.h>
#include <kernel/cmdline.h>
#include <kernel/range_check.h>
#include <platform.h>
#include <pretty/sizes.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <vm/bootreserve.h>
#include <vm/pmm.h>
#include <vm/vm.h>

// The max bytes of memory allowed by the system. Since it's specified in MB via the command
// line argument it will always be page aligned.
static size_t SystemMemoryLimit = 0;
// On init this is set to the memory limit and then decremented as we add memory to the system.
static size_t SystemMemoryRemaining = 0;
static bool MemoryLimitDbg = false;

typedef struct reserve_entry {
    uintptr_t start;     // Start of the reserved range
    size_t len;          // Length of the reserved range, does not change as start/end are adjusted
    uintptr_t end;       // End of the reserved range
    size_t unused_front; // Space before the region that is available
    size_t unused_back;  // Space after the region that is available
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
    for (size_t i = 0; i < ReservedRegionCount; i++) {
        const auto& entry = ReservedRegions[i];
        printf("%zu: [f: %-#10" PRIxPTR " |%#10" PRIxPTR " - %-#10" PRIxPTR "| (len: %#10" PRIxPTR
                ") b: %-#10" PRIxPTR "]\n", i, entry.unused_front, entry.start, entry.end,
                entry.len, entry.unused_back);
    }
}

zx_status_t memory_limit_init() {
    if (!SystemMemoryLimit) {
        ReservedRegionCount = 0;
        SystemMemoryLimit = cmdline_get_uint64("kernel.memory-limit-mb", 0u) * MB;
        if (!SystemMemoryLimit) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        MemoryLimitDbg = cmdline_get_bool("kernel.memory-limit-dbg", false);
        SystemMemoryRemaining = SystemMemoryLimit;
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

zx_status_t memory_limit_add_range(uintptr_t range_base,
                                   size_t range_size,
                                   pmm_arena_info_t arena_template) {
    // Arenas passed to us should never overlap. For that reason we can get a good idea of whether
    // a given memory limit can fit all the reserved regions by getting the total.
    auto cb = [range_base, range_size](reserve_range_t reserve) {
        // Is this reserved region in the arena?
        uintptr_t in_offset;
        size_t in_len;
        // If there's no intersection then move on to the next reserved region.
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
            uintptr_t start = fbl::max(range_base, prev.end);
            if (start == prev.end) {
                // We're next to someone! Figure out the gap space and share some of it with them.
                // This prevents corner case situations such as reserved regions on the edge of the
                // start or end, or where an expanded region may almost line up with the region
                // following it, but takes up enough space that the remaining region has no space to
                // allocate pages for its own bookkeeping. It also simplifies the logic for growing
                // space later, and results in less 'cheating' if we've allocated all of our
                // specified space and have to add room for a region's bookkeeping regardless. These
                // problems can be resolved in other ways, but they require extra passes, or more
                // complicated solutions.
                size_t spare_pages = (reserve.pa - start) / PAGE_SIZE;
                entry.unused_front = (spare_pages / 2) * PAGE_SIZE;
                prev.unused_back = (spare_pages / 2) * PAGE_SIZE;
                // If the page count was odd, account for the remaining page.
                if (spare_pages & 0x1) {
                    entry.unused_front += PAGE_SIZE;
                }
            } else {
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

    // If there's still space between the last reserved region in an arena and the end of the
    // arena then it should be accounted for in that last reserved region.
    if (ReservedRegionCount) {
        auto& last_entry = ReservedRegions[ReservedRegionCount - 1];
        if (Intersects(range_base, range_size, last_entry.start, last_entry.end)) {
            last_entry.unused_back = (range_base + range_size) - last_entry.end;
        }
    }

    if (MemoryLimitDbg) {
        printf("MemoryLimit: Processed arena [%#" PRIxPTR " - %#" PRIxPTR "]\n", range_base,
               range_base + range_size);
    }

    return ZX_OK;
}

zx_status_t memory_limit_add_arenas(pmm_arena_info_t arena_template) {
    // First pass, add up the memory needed for reserved ranges
    size_t required_for_reserved = 0;
    for (size_t i = 0; i < ReservedRegionCount; i++) {
        const auto& entry = ReservedRegions[i];
        required_for_reserved += (entry.end - entry.start);
    }

    char lim[16];
    printf("MemoryLimit: Limit of %s provided by kernel.memory-limit-mb\n",
            format_size(lim, sizeof(lim), SystemMemoryRemaining));
    if (required_for_reserved > SystemMemoryRemaining) {
        char req[16];
        printf("MemoryLimit: reserved regions need %s at a minimum!\n",
                format_size(req, sizeof(req), required_for_reserved));
        return ZX_ERR_NO_MEMORY;
    }

    SystemMemoryRemaining -= required_for_reserved;
    if (MemoryLimitDbg) {
        printf("MemoryLimit: First Pass, %#" PRIxPTR " remaining\n", SystemMemoryRemaining);
        print_reserve_state();
    }

    // Second pass, expand to take memory from the front / back of each region
    for (size_t i = 0; i < ReservedRegionCount; i++) {
        auto& entry = ReservedRegions[i];
        // Now expand based on any remaining memory we have to spare from the front
        // and back of the reserved region.
        size_t available = fbl::min(SystemMemoryRemaining, entry.unused_front);
        if (available) {
            SystemMemoryRemaining -= available;
            entry.unused_front -= available;
            entry.start = PAGE_ALIGN(entry.start - available);
        }

        available = fbl::min(SystemMemoryRemaining, entry.unused_back);
        if (available) {
            SystemMemoryRemaining -= available;
            entry.unused_back -= available;
            entry.end = PAGE_ALIGN(entry.end + available);
        }

        // Calculate how many pages are needed to hold the vm_page_t entries for this range
        size_t pages_needed = ROUNDUP_PAGE_SIZE((entry.len / PAGE_SIZE) * sizeof(vm_page_t));
        // Now add extra pages to account for pages added in the previous step
        const size_t vm_pages_per_page = PAGE_SIZE / sizeof(vm_page_t);
        pages_needed += (pages_needed + vm_pages_per_page - 1) / vm_pages_per_page;
        // Check if there is enough space in the range to hold the reserve region's bookkeeping
        // in a contiguous block on either side of it. If necessary, add some space if possible.
        size_t needed = ROUNDUP_PAGE_SIZE(((entry.len * 101) / 100));
        if (needed > (entry.end - entry.start)) {
            size_t pages_needed = (entry.len / PAGE_SIZE);
            size_t diff = needed - entry.len;
            printf("MemoryLimit: %zu needs %#zx for bookkeeping still (%zu pages)\n", i, diff, pages_needed);
            if (entry.unused_front > diff) {
                entry.unused_front -= diff;
                entry.start -= diff;
            } else if (entry.unused_back > diff) {
                entry.unused_back -= diff;
                entry.end += diff;
            } else {
                printf("KMemoryLimit: Unable to grow %zu to fit bookkeeping. Need %#" PRIxPTR "\n",
                       i, diff);
                return ZX_ERR_NO_MEMORY;
            }
        }
    }

    if (MemoryLimitDbg) {
        printf("MemoryLimit: Second Pass, %#" PRIxPTR " remaining\n", SystemMemoryRemaining);
        print_reserve_state();
    }

    // Third pass, coalesce the regions into the smallest number of arenas possible
    for (size_t i = 0; i < ReservedRegionCount - 1; i++) {
        auto& cur = ReservedRegions[i];
        auto& next = ReservedRegions[i + 1];

        if (cur.end == next.start) {
            printf("SystemMemoryLimit: merging |%#" PRIxPTR " - %#" PRIxPTR "| and |%#" PRIxPTR " - %#"
                   PRIxPTR "|\n", cur.start, cur.end, next.start, next.end);
            cur.end = next.end;
            memmove(&ReservedRegions[i + 1], &ReservedRegions[i + 2],
                    sizeof(reserve_entry_t) * (ReservedRegionCount - i - 2));
            // We've removed on entry and we also need to compare this new current entry to the new
            // next entry. To do so, we hold our position in the loop and come around again.
            ReservedRegionCount--;
            i--;
        }
    }

    if (MemoryLimitDbg) {
        printf("MemoryLimit: Third Pass\n");
        print_reserve_state();
        printf("MemoryLimit: Fourth Pass\n");
    }

    // Last pass, add arenas to the system
    for (uint i = 0; i < ReservedRegionCount; i++) {
        auto& entry = ReservedRegions[i];
        size_t size = entry.end - entry.start;
        if (MemoryLimitDbg) {
            printf("MemoryLimit: adding [%#" PRIxPTR " - %#" PRIxPTR "]\n", entry.start, entry.end);
        }
        zx_status_t status = add_arena(entry.start, size, arena_template);
        if (status != ZX_OK) {
            printf("MemoryLimit: Failed to add arena [%#" PRIxPTR " - %#" PRIxPTR
                   "]: %d, system problems may result!\n",
                   entry.start, entry.end, status);
        }
    }

    return ZX_OK;
}
