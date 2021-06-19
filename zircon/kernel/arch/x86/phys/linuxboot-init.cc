// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/allocator.h>
#include <stdio.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <phys/main.h>
#include <phys/symbolize.h>

#include "legacy-boot.h"
#include "linuxboot.h"

namespace {

zbi_mem_range_t FromE820(const e820entry_t& in) {
  zbi_mem_range_t out = {.paddr = in.addr, .length = in.size};

  switch (in.type) {
    case E820_RAM:
      out.type = ZBI_MEM_RANGE_RAM;
      break;

    case E820_RESERVED:
    default:
      // There are other E820_* types but none indicates usable RAM and
      // none corresponds to ZBI_MEM_RANGE_PERIPHERAL.
      out.type = ZBI_MEM_RANGE_RESERVED;
      break;
  }

  return out;
}

// The E820 table corresponds directly to the zbi_mem_range_t table
// semantically (and nearly in format), except that E820 entries are only 20
// bytes long while zbi_mem_range_t entries are aligned properly for 64-bit
// use at 24 bytes long.  So there isn't space to rewrite the data in place.
// However, the boot_params format has a fixed table size anyway, so a table
// in the shim's own bss can be used to store the normalized entries.
static_assert(sizeof(zbi_mem_range_t) > sizeof(e820entry_t),
              "could rewrite in place if entry sizes matched");

zbi_mem_range_t gMemRangesBuffer[linuxboot::kMaxE820TableEntries];

void PopulateMemRages(const linuxboot::boot_params& bp) {
  if (bp.e820_entries > ktl::size(bp.e820_table)) {
    printf("%s: e820_entries %zu exceeds format maximum %zu\n", Symbolize::kProgramName_,
           static_cast<size_t>(bp.e820_entries), ktl::size(bp.e820_table));
  }
  ktl::span e820{
      bp.e820_table,
      std::min(static_cast<size_t>(bp.e820_entries), ktl::size(bp.e820_table)),
  };

  // Translate the entries directly.
  size_t count = 0;
  for (const e820entry_t& in : e820) {
    if (in.size > 0) {
      gMemRangesBuffer[count++] = FromE820(in);
    }
  }

  gLegacyBoot.mem_config = ktl::span(gMemRangesBuffer).subspan(0, count);
}

ktl::string_view GetBootloaderName(const linuxboot::boot_params& bp) {
  static char bootloader_name[] = "Linux/x86 bzImage XXXX";

  uint8_t loader = bp.hdr.type_of_loader & 0xf0u;
  if (loader == 0xe0u) {
    loader = bp.hdr.ext_loader_type + 0x10u;
  }

  uint8_t version = (bp.hdr.type_of_loader & 0x0fu) + (bp.hdr.ext_loader_ver << 4);

  snprintf(&bootloader_name[sizeof(bootloader_name) - 5], 5, "%02hhx%02hhx", loader, version);

  return {bootloader_name, sizeof(bootloader_name) - 1};
}

}  // namespace

LegacyBoot gLegacyBoot;

// This populates the allocator and also collects other information.
void InitMemory(void* bootloader_data) {
  auto& bp = *static_cast<const linuxboot::boot_params*>(bootloader_data);

  // Synthesize a boot loader name from the few bits we get.
  gLegacyBoot.bootloader = GetBootloaderName(bp);

  if (bp.hdr.cmd_line_ptr != 0) {
    // The command line is NUL-terminated.
    gLegacyBoot.cmdline = reinterpret_cast<const char*>(bp.hdr.cmd_line_ptr);
  }

  if (bp.hdr.ramdisk_image != 0) {
    gLegacyBoot.ramdisk = {
        reinterpret_cast<ktl::byte*>(bp.hdr.ramdisk_image),
        bp.hdr.ramdisk_size,
    };
  }

  gLegacyBoot.acpi_rsdp = bp.acpi_rsdp_addr;

  // First translate the data into ZBI item format in gLegacyBoot.mem_config.
  PopulateMemRages(bp);

  // Now prime the allocator from that information.
  InitMemoryFromRanges();

  // Note this doesn't remove the memory covering the boot_params (zero page)
  // just examined.  We assume those have already been consumed as needed
  // before allocation starts.
}
