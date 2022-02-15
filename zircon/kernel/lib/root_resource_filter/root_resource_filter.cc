// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/root_resource_filter.h>
#include <lib/root_resource_filter_internal.h>
#include <stdio.h>
#include <trace.h>

#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <lk/init.h>
#include <phys/handoff.h>
#include <vm/pmm.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#define LOCAL_TRACE 0

KCOUNTER(resource_ranges_denied, "resource.denied_ranges")

namespace {
// The global singleton filter
RootResourceFilter g_root_resource_filter;
}  // namespace

void RootResourceFilter::Finalize() {
  // Add the PMM arenas as regions we may not allocate from.
  for (size_t i = 0, arena_count = pmm_num_arenas(); i < arena_count; ++i) {
    pmm_arena_info_t info;

    // There is no  reason for this to ever fail.
    zx_status_t res = pmm_get_arena_info(1, i, &info, sizeof(info));
    ASSERT(res == ZX_OK);

    // Add the arena to the set of regions to deny, permitting it to merge with
    // any pre-existing regions already in the set (shouldn't happen, but if it
    // does, we want the union). If we cannot add the arena to our set of
    // regions to deny, it can only be because we failed a heap allocation which
    // should be impossible at this point. If it does happen, panic. We cannot
    // run if we cannot enforce the deny list.
    res = mmio_deny_.AddRegion({.base = info.base, .size = info.size},
                               RegionAllocator::AllowOverlap::Yes);
    ASSERT(res == ZX_OK);
  }

  for (const zbi_mem_range_t& mem_range : gPhysHandoff->mem_config.get()) {
    if (mem_range.type == ZBI_MEM_RANGE_RESERVED) {
      mmio_deny_.SubtractRegion({.base = mem_range.paddr, .size = mem_range.length},
                                RegionAllocator::AllowIncomplete::Yes);
    }
  }

  // Dump the deny list at spew level for debugging purposes.
  if (DPRINTF_ENABLED_FOR_LEVEL(SPEW)) {
    dprintf(SPEW, "Final MMIO Deny list is:\n");
    mmio_deny_.WalkAvailableRegions([](const ralloc_region_t* region) -> bool {
      dprintf(SPEW, "Region [0x%lx, 0x%lx)\n", region->base, region->base + region->size);
      return true;  // Keep printing, don't stop now!
    });
  }
}

bool RootResourceFilter::IsRegionAllowed(uintptr_t base, size_t size, zx_rsrc_kind_t kind) const {
  // Currently, we only need to track denied mmio regions. Someday, this may
  // need to expand to other ranges as well (such as x64 IO ports)
  if (kind != ZX_RSRC_KIND_MMIO) {
    return true;
  }

  return !mmio_deny_.TestRegionIntersects({.base = base, .size = size},
                                          RegionAllocator::TestRegionSet::Available);
}

void root_resource_filter_add_deny_region(uintptr_t base, size_t size, zx_rsrc_kind_t kind) {
  // We only enforce deny regions for MMIO right now. In the future, if someone
  // wants to limit other regions as well (perhaps the I/O port space for x64),
  // they need to come back here and add another RegionAllocator instance to
  // enforce the rules for the new zone.
  ASSERT(kind == ZX_RSRC_KIND_MMIO);
  g_root_resource_filter.AddDenyRegion(base, size, kind);
}

bool root_resource_filter_can_access_region(uintptr_t base, size_t size, zx_rsrc_kind_t kind) {
  // Keep track of the number of regions that we end up denying. Typically, in
  // a properly operating system (aside from explicit tests) this should be 0.
  // Anything else probably indicates either malice or a bug somewhere.
  if (!g_root_resource_filter.IsRegionAllowed(base, size, kind)) {
    LTRACEF("WARNING - Denying range request [%016lx, %016lx) kind (%u)\n", base, base + size,
            kind);
    kcounter_add(resource_ranges_denied, 1);
    return false;
  }

  return true;
}

// Finalize the ZBI filter just before we start user mode. This will add the
// RAM regions described by the ZBI into the filter, and then subtract out
// the reserved RAM regions so that userspace can create MMIO resource ranges
// which target reserved RAM.
static void finalize_root_resource_filter(uint) { g_root_resource_filter.Finalize(); }
LK_INIT_HOOK(finalize_root_resource_filter, finalize_root_resource_filter, LK_INIT_LEVEL_USER - 1)
