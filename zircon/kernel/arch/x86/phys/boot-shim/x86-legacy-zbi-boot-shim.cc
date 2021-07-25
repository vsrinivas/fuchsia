// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/optional.h>
#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/main.h>
#include <phys/symbolize.h>

// This a shim between a ZBI protocol boot loader using the old x86 protocol
// and a bootable ZBI using the modern protocol.  It mostly just treats the
// data ZBI as a whole bootable ZBI and boots it using the modern ZBI booting
// protocol, which is always position-independent and fairly uniform across
// machines.  That means the original combined boot image contains two kernel
// items: this boot shim and then the actual kernel.
//
// In addition to being compatible with the old fixed-address loading protocol,
// this shim translates the older memory table formats into the modern one.

namespace {

// Scan the ZBI for one of the old memory table item types.  If we find one,
// we'll record it and then mark the original item as discarded.
ktl::optional<zbitl::MemRangeTable> FindLegacyTable(BootZbi::InputZbi zbi) {
  ktl::optional<zbitl::MemRangeTable> legacy_table;

  cpp20::span<ktl::byte> mutable_zbi{
      const_cast<ktl::byte*>(zbi.storage().data()),
      zbi.size_bytes(),
  };

  zbitl::View scan_zbi(mutable_zbi);
  for (auto it = scan_zbi.begin(); it != scan_zbi.end(); ++it) {
    if (it->header->type == ZBI_TYPE_E820_TABLE || it->header->type == ZBI_TYPE_EFI_MEMORY_MAP) {
      auto table = zbitl::MemRangeTable::FromSpan(it->header->type, it->payload);
      if (table.is_error()) {
        ktl::string_view type = zbitl::TypeName(it->header->type);
        printf("%s: Bad legacy %.*s item: %.*s\n", Symbolize::kProgramName_,
               static_cast<int>(type.size()), type.data(),
               static_cast<int>(table.error_value().size()), table.error_value().data());
      } else {
        legacy_table = table.value();
        auto result = scan_zbi.EditHeader(it, {.type = ZBI_TYPE_DISCARD});
        ZX_ASSERT(result.is_ok());
      }
    }
  }
  scan_zbi.ignore_error();

  return legacy_table;
}

}  // namespace

using MemConfigItem = boot_shim::SingleItem<ZBI_TYPE_MEM_CONFIG>;
using Shim = boot_shim::BootShim<MemConfigItem>;

const char Symbolize::kProgramName_[] = "x86-legacy-zbi-boot-shim";

void ZbiMain(void* zbi, arch::EarlyTicks boot_ticks) {
  BootZbi::InputZbi input_zbi(zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi)));

  ktl::optional<zbitl::MemRangeTable> legacy_table = FindLegacyTable(input_zbi);

  Shim shim(Symbolize::kProgramName_);

  if (!legacy_table) {
    // No legacy table, so there should be a normal ZBI_TYPE_MEM_CONFIG item.
    InitMemory(zbi);
  } else {
    // There was a legacy table rather than a modern table in the ZBI, so
    // InitMemory won't find it.  Populate a new table from the legacy table.
    // We can't do dynamic memory allocation yet.  The E820 table format has
    // entries smaller than zbi_mem_range_t, so we can't rewrite the data in
    // place.  So we have to pick a fixed maximum table size and preallocate.
    static zbi_mem_range_t mem_config_buffer[512];
    const size_t count = legacy_table->size();
    ZX_ASSERT_MSG(count <= ktl::size(mem_config_buffer),
                  "legacy table with %zu entries > fixed %zu shim table!", count,
                  ktl::size(mem_config_buffer));
    ktl::span mem_config{mem_config_buffer, count};
    ktl::copy(legacy_table->begin(), legacy_table->end(), mem_config.begin());
    ZbiInitMemory(zbi, mem_config);
    shim.Get<MemConfigItem>().set_payload(ktl::as_bytes(mem_config));
  }

  BootZbi boot;
  if (shim.Check("Not a bootable ZBI", boot.Init(input_zbi)) &&
      shim.Check("Failed to load ZBI", boot.Load(shim.size_bytes())) &&
      shim.Check("Failed to append boot loader items to data ZBI",
                 shim.AppendItems(boot.DataZbi()))) {
    boot.Boot();
  }

  abort();
}
