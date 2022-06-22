// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "legacy-boot.h"

#include <inttypes.h>
#include <lib/boot-shim/acpi.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/items/mem-config.h>
#include <zircon/assert.h>

#include <array>

#include <ktl/array.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/span.h>
#include <phys/acpi.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>
#include <phys/symbolize.h>
#include <phys/uart.h>
#include <pretty/sizes.h>

#include <ktl/enforce.h>

extern "C" {

// TODO(fxbug.dev/79166): In the linuxboot case, the linking logic gives the
// wrong value for PHYS_LOAD_ADDRESS in the linuxboot case; work around that
// for now.
[[gnu::weak]] extern ktl::byte LINUXBOOT_LOAD_ADDRESS[];

}  // extern "C"

namespace {

template <typename T>
ktl::span<const ktl::byte> AsBytes(const T& obj) {
  ktl::span span{ktl::data(obj), ktl::size(obj)};
  return ktl::as_bytes(span);
}

void InitAcpi(LegacyBoot& boot_info) {
  auto acpi_parser = MakeAcpiParser(boot_info.acpi_rsdp);
  if (acpi_parser.is_error()) {
    printf("%s: Cannot find ACPI tables (%" PRId32 ") from %#" PRIx64 "\n", ProgramName(),
           acpi_parser.error_value(), gLegacyBoot.acpi_rsdp);
    return;
  }
  boot_info.acpi_rsdp = acpi_parser->rsdp_pa();

  if (auto debug_port = acpi_lite::GetDebugPort(*acpi_parser); debug_port.is_ok()) {
    if (UartDriver driver; driver.Match(*debug_port)) {
      boot_info.uart = driver.uart();
      SetUartConsole(boot_info.uart);
    }
  }
}

}  // namespace

// A default, weak definition that may be overrode to perform other relevant
// initialization.
[[gnu::weak]] void LegacyBootSetUartConsole(const uart::all::Driver& uart) { SetUartConsole(uart); }

void LegacyBootInitMemory() {
  InitAcpi(gLegacyBoot);

  constexpr auto as_memrange =
      [](auto obj, memalloc::Type type = memalloc::Type::kLegacyBootData) -> memalloc::Range {
    auto bytes = AsBytes(obj);
    return {
        .addr = reinterpret_cast<uint64_t>(bytes.data()),
        .size = static_cast<uint64_t>(bytes.size()),
        .type = type,
    };
  };

  // TODO(fxbug.dev/79166): See LINUXBOOT_LOAD_ADDRESS comment above.
  uint64_t phys_start = &LINUXBOOT_LOAD_ADDRESS ? reinterpret_cast<uint64_t>(LINUXBOOT_LOAD_ADDRESS)
                                                : reinterpret_cast<uint64_t>(PHYS_LOAD_ADDRESS);
  uint64_t phys_end = reinterpret_cast<uint64_t>(_end);

  auto in_load_image = [phys_start, phys_end](auto obj) -> bool {
    auto bytes = AsBytes(obj);
    uint64_t start = reinterpret_cast<uint64_t>(bytes.data());
    uint64_t end = start + bytes.size();
    return phys_start <= start && end <= phys_end;
  };

  // Do not fill in the last three ranges in the array yet; we only need to
  // account for them if they do not lie within the shim's load image.
  memalloc::Range ranges[] = {
      {
          .addr = phys_start,
          .size = phys_end - phys_start,
          .type = memalloc::Type::kPhysKernel,
      },
      as_memrange(gLegacyBoot.ramdisk, memalloc::Type::kDataZbi),
      {},
      {},
      {},
  };
  size_t num_ranges = 2;
  if (!in_load_image(gLegacyBoot.cmdline)) {
    ranges[num_ranges++] = as_memrange(gLegacyBoot.cmdline);
  }
  if (!in_load_image(gLegacyBoot.bootloader)) {
    ranges[num_ranges++] = as_memrange(gLegacyBoot.bootloader);
  }
  if (!in_load_image(gLegacyBoot.mem_config)) {
    ranges[num_ranges++] = as_memrange(gLegacyBoot.mem_config);
  }

  Allocation::Init(memalloc::AsRanges(gLegacyBoot.mem_config),
                   ktl::span(ranges).subspan(0, num_ranges));
}
