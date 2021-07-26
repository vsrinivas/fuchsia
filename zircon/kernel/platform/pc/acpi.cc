// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/zircon.h>
#include <lib/console.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <ktl/optional.h>
#include <platform/pc/acpi.h>

namespace {

// System-wide ACPI parser.
acpi_lite::AcpiParser* global_acpi_parser;

lazy_init::LazyInit<acpi_lite::AcpiParser, lazy_init::CheckType::None,
                    lazy_init::Destructor::Disabled>
    g_parser;

int ConsoleAcpiDump(int argc, const cmd_args* argv, uint32_t flags) {
  if (global_acpi_parser == nullptr) {
    printf("ACPI not initialized.\n");
    return 1;
  }

  global_acpi_parser->DumpTables();
  return 0;
}

}  // namespace

acpi_lite::AcpiParser& GlobalAcpiLiteParser() {
  ASSERT_MSG(global_acpi_parser != nullptr, "PlatformInitAcpi() not called.");
  return *global_acpi_parser;
}

void PlatformInitAcpi(zx_paddr_t acpi_rsdp) {
  ASSERT(global_acpi_parser == nullptr);

  // Create AcpiParser.
  zx::status<acpi_lite::AcpiParser> result = acpi_lite::AcpiParserInit(acpi_rsdp);
  if (result.is_error()) {
    panic("Could not initialize ACPI. Error code: %d.", result.error_value());
  }

  g_parser.Initialize(result.value());
  global_acpi_parser = &g_parser.Get();
}

STATIC_COMMAND_START
STATIC_COMMAND("acpidump", "dump ACPI tables to console", &ConsoleAcpiDump)
STATIC_COMMAND_END(vm)
