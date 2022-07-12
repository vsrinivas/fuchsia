// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "legacy-boot-shim.h"

#include <lib/acpi_lite.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/memalloc/pool.h>
#include <lib/stdcompat/span.h>
#include <lib/uart/all.h>
#include <stdlib.h>
#include <zircon/boot/image.h>

#include <ktl/optional.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <phys/trampoline-boot.h>
#include <phys/uart.h>

#include "stdout.h"

#include <ktl/enforce.h>

void PhysMain(void* ptr, arch::EarlyTicks boot_ticks) {
  InitStdout();

  ApplyRelocations();

  MainSymbolize symbolize(kLegacyShimName);

  // This also fills in gLegacyBoot.
  InitMemory(ptr);

  UartFromZbi(LegacyBootShim::InputZbi(ktl::as_bytes(gLegacyBoot.ramdisk)), gLegacyBoot.uart);
  UartFromCmdLine(gLegacyBoot.cmdline, gLegacyBoot.uart);
  LegacyBootSetUartConsole(gLegacyBoot.uart);

  LegacyBootShim shim(symbolize.name(), gLegacyBoot);
  shim.set_build_id(symbolize.BuildIdString());
  shim.Get<boot_shim::UartItem>().Init(GetUartDriver().uart());

  // The pool knows all the memory details, so populate the ZBI item that way.
  memalloc::Pool& memory = Allocation::GetPool();
  shim.InitMemConfig(memory);

  TrampolineBoot boot;
  if (shim.Load(boot)) {
    ArchSetUpAddressSpaceLate();
    memory.PrintMemoryRanges(symbolize.name());
    boot.Log();
    boot.Boot();
  }

  abort();
}

bool LegacyBootShim::Load(TrampolineBoot& boot) {
  return BootQuirksLoad(boot) || StandardLoad(boot);
}

// This is overridden in the special bug-compatibility shim.
[[gnu::weak]] bool LegacyBootShim::BootQuirksLoad(TrampolineBoot& boot) { return false; }

bool LegacyBootShim::StandardLoad(TrampolineBoot& boot) {
  return Check("Not a bootable ZBI", boot.Init(input_zbi())) &&
         Check("Failed to load ZBI", boot.Load(size_bytes())) &&
         Check("Failed to append boot loader items to data ZBI", AppendItems(boot.DataZbi()));
}

bool LegacyBootShim::IsProperZbi() const {
  bool result = true;
  InputZbi zbi = input_zbi_;
  for (auto [header, payload] : zbi) {
    result = header->type == arch::kZbiBootKernelType;
    break;
  }
  zbi.ignore_error();
  return result;
}

// The default implementation assumes a conforming ZBI image, that is the first item is the kernel
// item, and items are appended. The symbols is weak, such that bug compatible shims can override
// this. Examples of bugs are bootloaders prepending items to the zbi image.
[[gnu::weak]] void UartFromZbi(LegacyBootShim::InputZbi zbi, uart::all::Driver& uart) {
  uart = GetUartFromRange(zbi.begin(), zbi.end()).value_or(uart);
  zbi.ignore_error();
}

ktl::optional<uart::all::Driver> GetUartFromRange(LegacyBootShim::InputZbi::iterator start,
                                                  LegacyBootShim::InputZbi::iterator end) {
  ktl::optional<uart::all::Driver> uart;
  UartDriver driver;
  while (start != end && start != start.view().end()) {
    if (auto& [header, payload] = *start; header->type == ZBI_TYPE_KERNEL_DRIVER) {
      if (driver.Match(*header, payload.data())) {
        uart = driver.uart();
      }
    }
    start++;
  }

  return uart;
}
