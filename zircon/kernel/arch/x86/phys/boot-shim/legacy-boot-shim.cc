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
#include <stdlib.h>

#include <phys/main.h>
#include <phys/symbolize.h>

#include "stdout.h"
#include "trampoline-boot.h"

namespace {

class PhysPhysMemReader final : public acpi_lite::PhysMemReader {
 public:
  constexpr PhysPhysMemReader() = default;

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) final {
    return zx::success(reinterpret_cast<const void*>(phys));
  }
};

void InitAcpi(LegacyBootShim& shim, uintptr_t rsdp) {
  PhysPhysMemReader mem_reader;
  auto acpi_parser = acpi_lite::AcpiParser::Init(mem_reader, rsdp);
  if (acpi_parser.is_ok()) {
    shim.InitAcpi(acpi_parser.value());
  } else {
    printf("%s: Cannot find ACPI tables (%" PRId32 ") from %#" PRIxPTR "\n",
           Symbolize::kProgramName_, acpi_parser.error_value(), rsdp);
  }
}

}  // namespace

void PhysMain(void* ptr, arch::EarlyTicks boot_ticks) {
  StdoutInit();

  ApplyRelocations();

  // This also fills in gLegacyBoot.
  InitMemory(ptr);

  StdoutFromCmdline(gLegacyBoot.cmdline);

  LegacyBootShim shim(Symbolize::kProgramName_, gLegacyBoot);
  shim.set_build_id(Symbolize::GetInstance()->BuildIdString());

  // If the RSDP address is 0, AcpiParser::Init will try to find it by magic.
  InitAcpi(shim, gLegacyBoot.acpi_rsdp);

  TrampolineBoot boot;
  if (shim.Check("Not a bootable ZBI", boot.Init(shim.input_zbi())) &&
      shim.Check("Failed to load ZBI", boot.Load(shim.size_bytes())) &&
      shim.Check("Failed to append boot loader items to data ZBI",
                 shim.AppendItems(boot.DataZbi()))) {
    EnablePaging();
    boot.Boot();
  }

  abort();
}
