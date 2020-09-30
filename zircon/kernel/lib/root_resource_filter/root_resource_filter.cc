// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/counters.h>
#include <lib/root_resource_filter.h>
#include <lib/root_resource_filter_internal.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <trace.h>

#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <lk/init.h>
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

  const zbi_header_t* zbi_container = platform_get_zbi();
  if (zbi_container) {
    ktl::span<const ktl::byte> zbi{reinterpret_cast<const std::byte*>(zbi_container),
                                   sizeof(*zbi_container) + zbi_container->length};
    zbitl::View view(zbi);
    for (auto [header, payload] : view) {
      if (header->type != ZBI_TYPE_MEM_CONFIG) {
        continue;
      }
      const zbi_mem_range_t* mem_range = reinterpret_cast<const zbi_mem_range_t*>(payload.data());
      const uint32_t count = header->length / static_cast<uint32_t>(sizeof(zbi_mem_range_t));

      for (uint32_t i = 0; i < count; i++, mem_range++) {
        if (mem_range->type == ZBI_MEM_RANGE_RESERVED) {
          mmio_deny_.SubtractRegion({.base = mem_range->paddr, .size = mem_range->length},
                                    RegionAllocator::AllowIncomplete::Yes);
        }
      }
    }
    if (auto result = view.take_error(); result.is_error()) {
      auto error = std::move(result).error_value();
      dprintf(INFO,
              "WARNING - error encountered while iterating over ZBI at offset"
              " %#x: %.*s. Reserved memory regions will not be removed from the"
              " resource deny list.\n",
              error.item_offset, static_cast<int>(error.zbi_error.size()), error.zbi_error.data());
    }
  } else {
    dprintf(INFO,
            "WARNING - platform failed to provide a pointer to the ZBI. Reserved memory regions "
            "will not be removed from the resource deny list.\n");
  }

  // Attempt to reserve any regions specified by the command line
  gCmdline.ProcessRamReservations(
      [this](size_t size, std::string_view name) { return ProcessCmdLineReservation(size, name); });

  // Dump the deny list at spew level for debugging purposes.
  if (DPRINTF_ENABLED_FOR_LEVEL(SPEW)) {
    dprintf(SPEW, "Final MMIO Deny list is:\n");
    mmio_deny_.WalkAvailableRegions([](const ralloc_region_t* region) -> bool {
      dprintf(SPEW, "Region [0x%lx, 0x%lx)\n", region->base, region->base + region->size);
      return true;  // Keep printing, don't stop now!
    });
  }
}

std::optional<uintptr_t> RootResourceFilter::ProcessCmdLineReservation(size_t size,
                                                                       std::string_view name) {
  // Sadly, the compiler's printf format string argument checking does not
  // understand the kernel's special %V extension for string view.  We
  // suppress the warning by indirecting through a local printf lambda which
  // is not annotated to have the check.
  auto Printf = [](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  };

  // Sanity check our args before proceeding.
  if (size & (static_cast<uintptr_t>(PAGE_SIZE) - 1)) {
    Printf(
        "WARNING - RAM reservation \"%V\" request must be a multiple of page size (size=0x%zx).\n",
        name, size);
    return std::nullopt;
  }

  // Create the node we will use to hold our pointer and unpin our VMO on destruction.
  fbl::AllocChecker ac;
  auto node = ktl::make_unique<CommandLineReservedRegion>(&ac);
  if (!ac.check()) {
    Printf("WARNING - Failed to allocate storage for command line RAM reservation \"%V\"\n", name);
    return std::nullopt;
  }

  // Attempt to allocate a contiguous region of RAM to satisfy this reservation.
  // Do not store the result in the node just yet, we want to make sure that the
  // pin succeeds first (since the node destructor will unconditionally unpin
  // the VMO).
  fbl::RefPtr<VmObjectPaged> tmp;
  zx_status_t status;
  status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, size, PAGE_SIZE_SHIFT, &tmp);
  if (status == ZX_OK) {
    // Make sure that we have pages backing this VMO and that they are pinned.
    // We want to make sure that this memory is off limits to the PMM from here
    // on out.
    status = tmp->CommitRangePinned(0, size);
    if (status == ZX_OK) {
      node->vmo = std::move(tmp);
    }
  }

  if (status != ZX_OK) {
    Printf("WARNING - Failed to reserve RAM for command line reservation \"%V\" (status=%d)\n",
           name, status);
    return std::nullopt;
  }

  // Great, we have the region reserved.  All we need to do now is to update the
  // deny list.  Start by fetching the phys addr of the region we allocated and
  // pinned.
  paddr_t phys;
  status = node->vmo->GetPage(0, 0, nullptr, nullptr, nullptr, &phys);
  if (status != ZX_OK) {
    Printf("WARNING - Failed to fetch physaddr for command line reservation \"%V\" (status=%d)\n",
           name, status);
    return std::nullopt;
  }

  // Allow user mode access to the RAM we just reserved.
  status = mmio_deny_.SubtractRegion({.base = phys, .size = size},
                                     RegionAllocator::AllowIncomplete::Yes);
  if (status != ZX_OK) {
    Printf(
        "WARNING - Failed to add region [%lx, %lx) command line reservation \"%V\" to deny "
        "list (status=%d)\n",
        phys, phys + size, name, status);
    return std::nullopt;
  }

  // Everything went well.  Hold onto the VMO we are using to enforce our
  // reservation and return the address we reserved.
  Printf("Created command line RAM reservation \"%V\" at [%lx, %lx)\n", name, phys, phys + size);
  cmd_line_reservations_.push_front(std::move(node));
  return phys;
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
