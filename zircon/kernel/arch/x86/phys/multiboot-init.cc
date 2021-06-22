// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc/allocator.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/multiboot.h>

#include <ktl/atomic.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/symbolize.h>

#include "legacy-boot.h"

LegacyBoot gLegacyBoot;

// This populates the allocator and also collects other Multiboot information.
void InitMemory(void* bootloader_data) {
  const auto& info = *static_cast<const multiboot_info_t*>(bootloader_data);

  if ((info.flags & MB_INFO_BOOT_LOADER) && info.boot_loader_name != 0) {
    gLegacyBoot.bootloader = reinterpret_cast<const char*>(info.boot_loader_name);
  }

  if ((info.flags & MB_INFO_CMD_LINE) && info.cmdline != 0) {
    gLegacyBoot.cmdline = reinterpret_cast<const char*>(info.cmdline);
  }

  if ((info.flags & MB_INFO_MMAP) && info.mmap_addr != 0 &&
      info.mmap_length >= sizeof(memory_map_t)) {
    // If the map of discontiguous ranges is present, it covers everything.
    // The entry size fits the ZBI format, so convert each entry in place.
    static_assert(sizeof(memory_map_t) >= sizeof(zbi_mem_range_t));

    auto mem_ranges = reinterpret_cast<zbi_mem_range_t*>(info.mmap_addr);
    size_t count = 0;

    auto m = reinterpret_cast<const memory_map_t*>(info.mmap_addr);
    const auto end = reinterpret_cast<const memory_map_t*>(info.mmap_addr + info.mmap_length);
    while (m < end) {
      zbi_mem_range_t entry = {
          .paddr = (uint64_t{m->base_addr_high} << 32) | m->base_addr_low,
          .length = (uint64_t{m->length_high} << 32) | m->length_low,
      };

      switch (m->type) {
        case MB_MMAP_TYPE_AVAILABLE:
          entry.type = ZBI_MEM_RANGE_RAM;
          break;

        case MB_MMAP_TYPE_RESERVED:
        default:
          // There are other MB_MMAP_TYPE_* types but none indicates usable RAM
          // and none corresponds to ZBI_MEM_RANGE_PERIPHERAL.
          entry.type = ZBI_MEM_RANGE_RESERVED;
          break;
      }

      m = reinterpret_cast<const memory_map_t*>(  //
          reinterpret_cast<const char*>(&m->size + 1) + m->size);

      // Tell the compiler not to move any of the memory accesses after the
      // store below, since we're violating the TBAA rules and the compiler
      // could otherwise presume that they don't overlap.
      ktl::atomic_signal_fence(ktl::memory_order_seq_cst);

      mem_ranges[count++] = entry;
    }

    gLegacyBoot.mem_config = {mem_ranges, count};
  } else if (info.flags & MB_INFO_MEM_SIZE) {
    // Without the full map, there are only the fixed low areas of memory.
    static zbi_mem_range_t low_ranges[] = {
        // Lower memory is from 0 to 640K.
        {
            .paddr = 0,
            .length = info.mem_lower << 10,
            .type = ZBI_MEM_RANGE_RAM,
        },

        // Upper memory is above 1M.
        {
            .paddr = 1 << 20,
            .length = info.mem_upper << 10,
            .type = ZBI_MEM_RANGE_RAM,
        },
    };
    gLegacyBoot.mem_config = low_ranges;
  }

  if ((info.flags & MB_INFO_MODS) && info.mods_addr != 0) {
    ktl::span mods{
        reinterpret_cast<const module_t*>(info.mods_addr),
        info.mods_count,
    };
    if (!mods.empty()) {
      gLegacyBoot.ramdisk = {
          reinterpret_cast<ktl::byte*>(mods.front().mod_start),
          mods.front().mod_end - mods.front().mod_start,
      };
      if (mods.size() > 1) {
        printf("%s: Multiboot mods @ %p count %zu != expected 1.\n", Symbolize::kProgramName_,
               mods.data(), mods.size());
      }
    }
  }

  // The depthcharge-multiboot shim needs some bug-compatibility adjustments.
  if (&LegacyBootQuirks) {
    LegacyBootQuirks();
  }

  InitMemoryFromRanges();

  // Note this doesn't remove the memory covering the Multiboot info itself or
  // the memory map or module list data just examined.  We assume those have
  // already been consumed as needed before allocation starts.
}
