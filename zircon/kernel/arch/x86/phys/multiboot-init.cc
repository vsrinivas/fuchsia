// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/multiboot.h>

#include <phys/allocation.h>
#include <phys/main.h>

#include "legacy-boot.h"

LegacyBoot gLegacyBoot;

// This populates the allocator and also collects other Multiboot information.
void InitMemory(void* bootloader_data) {
  const auto& info = *static_cast<const multiboot_info_t*>(bootloader_data);

  auto& allocator = Allocation::GetAllocator();
  auto add_range = [&allocator](uint64_t base, uint64_t size, auto what) {
    ZX_ASSERT_MSG(allocator.AddRange(base, size).is_ok(),
                  "Cannot add %s range [%#" PRIx64 ", %#" PRIx64 ")\n", what, base, size);
    printf("  paddr: [0x%016" PRIx64 " -- 0x%016" PRIx64 ") : size %10" PRIu64 " kiB : %s added\n",
           base, base + size, size >> 10, what);
  };
  auto remove_range = [&allocator](uint64_t base, uint64_t size, auto what) {
    ZX_ASSERT_MSG(allocator.RemoveRange(base, size).is_ok(),
                  "Cannot remove %s range [%#" PRIx64 ", %#" PRIx64 ")\n", what, base, size);
    printf("  paddr: [0x%016" PRIx64 " -- 0x%016" PRIx64 ") : size %10" PRIu64
           " kiB : %s removed\n",
           base, base + size, size >> 10, what);
  };

  // Handle the fixed ranges, if present.
  if (info.flags & MB_INFO_MEM_SIZE) {
    // Lower memory is from 0 to 640K.
    add_range(0, info.mem_lower << 10, "mem_lower");

    // Upper memory is above 1M.
    add_range(1 << 20, (info.mem_upper << 10) - (1 << 20), "mem_upper");
  }

  // Iterate over the discontiguous ranges, if present.
  auto for_each_mmap = [&info](auto&& f) {
    if (info.mmap_addr != 0 && info.mmap_length >= sizeof(memory_map_t) &&
        (info.flags & MB_INFO_MMAP)) {
      auto first = reinterpret_cast<const memory_map_t*>(info.mmap_addr);
      auto last = reinterpret_cast<const memory_map_t*>(info.mmap_addr + info.mmap_length);
      constexpr auto next = [](const memory_map_t* m) {
        return reinterpret_cast<const memory_map_t*>(  //
            reinterpret_cast<const char*>(&m->base_addr_low) + m->size);
      };
      for (auto m = first; m < last; m = next(m)) {
        uint64_t base = (uint64_t{m->base_addr_high} << 32) | m->base_addr_low;
        uint64_t size = (uint64_t{m->length_high} << 32) | m->length_low;
        f(m->type, base, size);
      }
    }
  };

  // Add normal memory first.
  for_each_mmap([&](auto type, auto base, auto size) {
    if (type == MB_MMAP_TYPE_AVAILABLE) {
      add_range(base, size, "mmap");
    }
  });

  // Now remove everything else, in case it overlapped.
  for_each_mmap([&](auto type, auto base, auto size) {
    if (type != MB_MMAP_TYPE_AVAILABLE) {
      remove_range(base, size, "mmap");
    }
  });

  // Remove the memory occupied by modules (i.e. the ZBI).
  if ((info.flags & MB_INFO_MODS) && info.mods_addr != 0) {
    ktl::span mods{
        reinterpret_cast<const module_t*>(info.mods_addr),
        info.mods_count,
    };
    for (const auto& m : mods) {
      uintptr_t base = m.mod_start, size = m.mod_end - m.mod_start;
      remove_range(base, size, "mod");
      gLegacyBoot.ramdisk = {reinterpret_cast<ktl::byte*>(base), size};
    }
    if (mods.size() != 1) {
      printf("Multiboot mods @ %p count %zu != expected 1.\n", mods.data(), mods.size());
    }
  }

  // Remove the memory occupied by the Multiboot command line, if present.
  // It will be copied into the data ZBI, but that requires allocation.
  // The same goes for the boot loader name, if present.

  auto collect_string = [&](uintptr_t paddr, auto what) {
    ktl::string_view result;
    if (paddr != 0) {
      result = reinterpret_cast<const char*>(paddr);
      if (!result.empty()) {
        remove_range(paddr, result.size() + 1, what);
      }
    }
    return result;
  };

  if (info.flags & MB_INFO_CMD_LINE) {
    gLegacyBoot.cmdline = collect_string(info.cmdline, "cmdline");
  }

  if (info.flags & MB_INFO_BOOT_LOADER) {
    gLegacyBoot.bootloader = collect_string(info.boot_loader_name, "boot_loader_name");
  }

  // Remove space occupied by the program itself.
  Allocation::InitReservedRanges();

  // Remove everything above the 32-bit barrier since we can't use it directly.
  constexpr uint64_t k4GiB = uint64_t{1} << 32;
  remove_range(k4GiB, -k4GiB, "high 32 bits");

  // Note this doesn't remove the memory covering the Multiboot info itself or
  // the memory map or module list data just examined.  We assume those have
  // already been consumed as needed before allocation starts.
}
