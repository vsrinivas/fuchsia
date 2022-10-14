// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "memory.h"

#include <assert.h>
#include <inttypes.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/memory_limit.h>
#include <lib/zbitl/items/mem-config.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/x86/bootstrap16.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <dev/interrupt.h>
#include <efi/boot-services.h>
#include <fbl/algorithm.h>
#include <kernel/range_check.h>
#include <ktl/iterator.h>
#include <lk/init.h>
#include <object/handle.h>
#include <object/resource_dispatcher.h>
#include <vm/bootreserve.h>
#include <vm/vm.h>

#include "platform_p.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// These are used to track memory arenas found during boot so they can
// be exclusively reserved within the resource system after the heap
// has been initialized.
constexpr uint8_t kMaxReservedMmioEntries = 64;
typedef struct reserved_mmio_space {
  uint64_t base;
  size_t len;
  KernelHandle<ResourceDispatcher> handle;
} reserved_mmio_space_t;
reserved_mmio_space_t reserved_mmio_entries[kMaxReservedMmioEntries];
static uint8_t reserved_mmio_count = 0;

constexpr uint8_t kMaxReservedPioEntries = 64;
typedef struct reserved_pio_space {
  uint64_t base;
  size_t len;
  KernelHandle<ResourceDispatcher> handle;
} reserved_pio_space_t;
reserved_pio_space_t reserved_pio_entries[kMaxReservedPioEntries];
static uint8_t reserved_pio_count = 0;

void mark_mmio_region_to_reserve(uint64_t base, size_t len) {
  ZX_DEBUG_ASSERT(reserved_mmio_count < kMaxReservedMmioEntries);
  reserved_mmio_entries[reserved_mmio_count].base = base;
  reserved_mmio_entries[reserved_mmio_count].len = len;
  reserved_mmio_count++;
}

void mark_pio_region_to_reserve(uint64_t base, size_t len) {
  ZX_DEBUG_ASSERT(reserved_pio_count < kMaxReservedPioEntries);
  reserved_pio_entries[reserved_pio_count].base = base;
  reserved_pio_entries[reserved_pio_count].len = len;
  reserved_pio_count++;
}

#define DEFAULT_MEMEND (16UL * 1024 * 1024)

// Populate global memory arenas from the given memory ranges.
static zx_status_t mem_arena_init(ktl::span<const zbi_mem_range_t> ranges) {
  // Determine if the user has given us an artificial limit on the amount of memory we can use.
  bool have_limit = (memory_limit_init() == ZX_OK);

  // Create the kernel's singleton for address space management.
  pmm_arena_info_t base_arena;
  snprintf(base_arena.name, sizeof(base_arena.name), "%s", "memory");
  base_arena.flags = 0;

  LTRACEF("%zu memory ranges from physboot\n", ranges.size());
  zbitl::MemRangeMerger merged_ranges(ranges.begin(), ranges.end());
  // First process all the reserved ranges. We do this in case there are reserved regions that
  // overlap with the RAM regions that occur later in the list. If we didn't process the reserved
  // regions first, then we might add a pmm arena and have it carve out its vm_page_t array from
  // what we will later learn is reserved memory.
  for (const zbi_mem_range_t& range : merged_ranges) {
    LTRACEF("Range at %#" PRIx64 " of %#" PRIx64 " bytes is %sreserved.\n", range.paddr,
            range.length, range.type == ZBI_MEM_RANGE_RESERVED ? "" : "not ");
    if (range.type == ZBI_MEM_RANGE_RESERVED) {
      boot_reserve_add_range(range.paddr, range.length);
    }
  }
  for (const zbi_mem_range_t& range : merged_ranges) {
    LTRACEF("Range at %#" PRIx64 " of %#" PRIx64 " bytes is %smemory.\n", range.paddr, range.length,
            range.type == ZBI_MEM_RANGE_RAM ? "" : "not ");
    if (range.type != ZBI_MEM_RANGE_RAM) {
      continue;
    }

    // trim off parts of memory ranges that are smaller than a page
    uint64_t base = ROUNDUP(range.paddr, PAGE_SIZE);
    uint64_t size = ROUNDDOWN(range.paddr + range.length, PAGE_SIZE) - base;

    // trim any memory below 1MB for safety and SMP booting purposes
    if (base < 1 * MB) {
      uint64_t adjust = 1 * MB - base;
      if (adjust >= size)
        continue;

      base += adjust;
      size -= adjust;
    }

    mark_mmio_region_to_reserve(base, static_cast<size_t>(size));
    zx_status_t status = ZX_OK;
    if (have_limit) {
      status = memory_limit_add_range(base, size, base_arena);
    }

    // If there is no limit, or we failed to add arenas from processing
    // ranges then add the original range.
    if (!have_limit || status != ZX_OK) {
      auto arena = base_arena;
      arena.base = base;
      arena.size = size;

      LTRACEF("Adding pmm range at %#" PRIxPTR " of %#zx bytes.\n", arena.base, arena.size);
      status = pmm_add_arena(&arena);

      // print a warning and continue
      if (status != ZX_OK) {
        printf("MEM: Failed to add pmm range at %#" PRIxPTR " size %#zx\n", arena.base, arena.size);
      }
    }
  }

  if (have_limit) {
    memory_limit_add_arenas(base_arena);
  }

  return ZX_OK;
}

// Discover the basic memory map.
void pc_mem_init(ktl::span<const zbi_mem_range_t> ranges) {
  pmm_checker_init_from_cmdline();

  // If no ranges were provided, use a fixed-size fallback range.
  if (ranges.empty()) {
    printf("MEM: no arena range source: falling back to fixed size\n");
    static zbi_mem_range_t entry = {};
    entry.paddr = 0;
    entry.length = DEFAULT_MEMEND;
    entry.type = ZBI_MEM_RANGE_RAM;
    ranges = ktl::span<zbi_mem_range_t>(&entry, 1);
  }

  // Initialize memory from the ranges provided in the ZBI.
  if (zx_status_t status = mem_arena_init(ranges); status != ZX_OK) {
    TRACEF("Error adding arenas from provided memory tables: error = %d\n", status);
  }

  // Find an area that we can use for 16 bit bootstrapping of other SMP cores.
  bool initialized_bootstrap16 = false;
  constexpr uint64_t kAllocSize = k_x86_bootstrap16_buffer_size;
  constexpr uint64_t kMinBase = 2UL * PAGE_SIZE;
  for (const auto& range : ranges) {
    // Ignore ranges that are not normal RAM.
    if (range.type != ZBI_MEM_RANGE_RAM) {
      continue;
    }

    // Only consider parts of the range that are in [kMinBase, 1MiB).
    uint64_t base, length;
    bool overlap =
        GetIntersect(kMinBase, 1 * MB - kMinBase, range.paddr, range.length, &base, &length);
    if (!overlap) {
      continue;
    }

    // Ignore ranges that are too small.
    if (length < kAllocSize) {
      continue;
    }

    // We have a valid range.
    LTRACEF("Selected %" PRIxPTR " as bootstrap16 region\n", base);
    x86_bootstrap16_init(base);
    initialized_bootstrap16 = true;
    break;
  }
  if (!initialized_bootstrap16) {
    TRACEF("WARNING - Failed to assign bootstrap16 region, SMP won't work\n");
  }
}

// Initialize the higher level PhysicalAspaceManager after the heap is initialized.
static void x86_resource_init_hook(unsigned int rl) {
  // An error is likely fatal if the bookkeeping is broken and driver
  ResourceDispatcher::InitializeAllocator(
      ZX_RSRC_KIND_MMIO, 0,
      (1ull << arch::BootCpuid<arch::CpuidAddressSizeInfo>().phys_addr_bits()) - 1);
  ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IOPORT, 0, UINT16_MAX);
  ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_SYSTEM, 0, ZX_RSRC_SYSTEM_COUNT);

  // Exclusively reserve the regions marked as memory earlier so that physical
  // vmos cannot be created against them.
  for (uint8_t i = 0; i < reserved_mmio_count; i++) {
    zx_rights_t rights;
    auto& entry = reserved_mmio_entries[i];

    zx_status_t st =
        ResourceDispatcher::Create(&entry.handle, &rights, ZX_RSRC_KIND_MMIO, entry.base, entry.len,
                                   ZX_RSRC_FLAG_EXCLUSIVE, "platform_memory");
    if (st != ZX_OK) {
      TRACEF("failed to create backing resource for boot memory region %#lx - %#lx: %d\n",
             entry.base, entry.base + entry.len, st);
    }
  }

  // Exclusively reserve io ports in use
  for (uint8_t i = 0; i < reserved_pio_count; i++) {
    zx_rights_t rights;
    auto& entry = reserved_pio_entries[i];

    zx_status_t st =
        ResourceDispatcher::Create(&entry.handle, &rights, ZX_RSRC_KIND_IOPORT, entry.base,
                                   entry.len, ZX_RSRC_FLAG_EXCLUSIVE, "platform_io_port");
    if (st != ZX_OK) {
      TRACEF("failed to create backing resource for io port region %#lx - %#lx: %d\n", entry.base,
             entry.base + entry.len, st);
    }
  }

  // debug_uart.irq needs to be reserved here. See fxbug.dev/33936.
}

LK_INIT_HOOK(x86_resource_init, x86_resource_init_hook, LK_INIT_LEVEL_HEAP)
