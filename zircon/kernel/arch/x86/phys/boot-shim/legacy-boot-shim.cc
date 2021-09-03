// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "legacy-boot-shim.h"

#include <lib/acpi_lite.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/memalloc/pool.h>
#include <stdlib.h>

#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>

#include "acpi.h"
#include "stdout.h"
#include "trampoline-boot.h"

void PhysMain(void* ptr, arch::EarlyTicks boot_ticks) {
  ConfigureStdout();

  ApplyRelocations();

  // This also fills in gLegacyBoot.
  InitMemory(ptr);

  StdoutFromCmdline(gLegacyBoot.cmdline);

  LegacyBootShim shim(Symbolize::kProgramName_, gLegacyBoot);
  shim.set_build_id(Symbolize::GetInstance()->BuildIdString());

  // The pool knows all the memory details, so populate the ZBI item that way.
  memalloc::Pool& memory = Allocation::GetPool();
  shim.InitMemConfig(memory);

  InitAcpi(shim);

  TrampolineBoot boot;
  if (shim.Load(boot)) {
    ArchSetUpAddressSpaceLate();
    memory.PrintMemoryRanges(Symbolize::kProgramName_);
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
