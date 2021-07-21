// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "legacy-boot-shim.h"

#include <inttypes.h>
#include <lib/acpi_lite.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/test-serial-number.h>
#include <lib/memalloc/pool.h>
#include <stdlib.h>

#include <phys/main.h>
#include <phys/symbolize.h>

#include "acpi.h"
#include "stdout.h"
#include "trampoline-boot.h"

void PhysMain(void* ptr, arch::EarlyTicks boot_ticks) {
  StdoutInit();

  ApplyRelocations();

  // This also fills in gLegacyBoot.
  InitMemory(ptr);

  StdoutFromCmdline(gLegacyBoot.cmdline);

  LegacyBootShim shim(Symbolize::kProgramName_, gLegacyBoot);
  shim.set_build_id(Symbolize::GetInstance()->BuildIdString());

  InitAcpi(shim);

  TrampolineBoot boot;
  if (shim.Check("Not a bootable ZBI", boot.Init(shim.input_zbi())) &&
      shim.Check("Failed to load ZBI", boot.Load(shim.size_bytes())) &&
      shim.Check("Failed to append boot loader items to data ZBI",
                 shim.AppendItems(boot.DataZbi()))) {
    EnablePaging();
    Allocation::GetPool().PrintMemoryRanges(Symbolize::kProgramName_);
    boot.Boot();
  }

  abort();
}
