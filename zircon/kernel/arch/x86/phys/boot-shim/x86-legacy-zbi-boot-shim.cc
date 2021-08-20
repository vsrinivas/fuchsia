// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/pool-mem-config.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
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

using MemConfigItem = boot_shim::PoolMemConfigItem;

using Shim = boot_shim::BootShim<MemConfigItem>;

// Populate a new table from the incoming table.  We can't do dynamic memory
// allocation yet.  The E820 table format has entries smaller than the modern
// zbi_mem_range_t entries, so we can't always rewrite the data in place.  So
// we have to pick a fixed maximum table size and preallocate .bss space.  To
// keep thing simple, we choose a large limit and do this for all formats, even
// though we could rewrite other formats in place (or use the modern format as
// is if we get it) and not have any fixed limit for those cases.

constexpr size_t kMaxMemConfigEntries = 512;
zbi_mem_range_t gMemConfigBuffer[kMaxMemConfigEntries];

ktl::span<zbi_mem_range_t> GetMemoryRanges(const zbitl::MemRangeTable& table) {
  const size_t count = table.size();
  ZX_ASSERT_MSG(count <= ktl::size(gMemConfigBuffer),
                "legacy table with %zu entries > fixed %zu shim table!", count,
                ktl::size(gMemConfigBuffer));
  ktl::span mem_config{gMemConfigBuffer, count};
  ktl::copy(table.begin(), table.end(), mem_config.begin());
  return mem_config;
}

// Scan the ZBI for any of the memory table item types, old or new.  If we find
// one, we'll record it and then mark the original item as discarded.
zbitl::MemRangeTable FindIncomingMemoryTable(BootZbi::InputZbi zbi) {
  zbitl::MemRangeTable table;

  cpp20::span<ktl::byte> mutable_zbi{
      const_cast<ktl::byte*>(zbi.storage().data()),
      zbi.size_bytes(),
  };

  zbitl::View scan_zbi(mutable_zbi);
  for (auto it = scan_zbi.begin(); it != scan_zbi.end(); ++it) {
    switch (it->header->type) {
      case ZBI_TYPE_MEM_CONFIG:
      case ZBI_TYPE_E820_TABLE:
      case ZBI_TYPE_EFI_MEMORY_MAP:
        break;
      default:
        continue;
    }
    auto result = zbitl::MemRangeTable::FromSpan(it->header->type, it->payload);
    if (result.is_error()) {
      ktl::string_view type = zbitl::TypeName(it->header->type);
      printf("%s: Bad legacy %.*s item: %.*s\n", Symbolize::kProgramName_,
             static_cast<int>(type.size()), type.data(),
             static_cast<int>(result.error_value().size()), result.error_value().data());
    } else {
      table = result.value();
      auto edit_result = scan_zbi.EditHeader(it, {.type = ZBI_TYPE_DISCARD});
      ZX_ASSERT(edit_result.is_ok());
    }
  }
  scan_zbi.ignore_error();

  return table;
}

ktl::span<zbi_mem_range_t> GetZbiMemoryRanges(BootZbi::InputZbi input_zbi) {
  return GetMemoryRanges(FindIncomingMemoryTable(input_zbi));
}

zbitl::ByteView GetInputZbi(void* zbi) {
  return zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi));
}

}  // namespace

const char Symbolize::kProgramName_[] = "x86-legacy-zbi-boot-shim";

void ZbiMain(void* zbi, arch::EarlyTicks boot_ticks) {
  BootZbi::InputZbi input_zbi(GetInputZbi(zbi));

  ZbiInitMemory(zbi, GetZbiMemoryRanges(input_zbi));

  Shim shim(Symbolize::kProgramName_);
  shim.set_build_id(Symbolize::GetInstance()->BuildIdString());

  // The pool knows all the memory details, so populate the new ZBI item that
  // way.  The incoming ZBI items in whatever format have been discarded.
  shim.Get<MemConfigItem>().Init(Allocation::GetPool());

  BootZbi boot;
  if (shim.Check("Not a bootable ZBI", boot.Init(input_zbi)) &&
      shim.Check("Failed to load ZBI", boot.Load(shim.size_bytes())) &&
      shim.Check("Failed to append boot loader items to data ZBI",
                 shim.AppendItems(boot.DataZbi()))) {
    boot.Boot();
  }

  abort();
}
