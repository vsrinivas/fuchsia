// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/zircon.h>
#include <lib/console.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <ktl/optional.h>
#include <platform/pc/acpi.h>

namespace {

// System-wide ACPI parser.
acpi_lite::AcpiParser* global_acpi_parser;

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
  // Create AcpiParser.
  static acpi_lite::AcpiParser parser = [acpi_rsdp]() {
    zx::status<acpi_lite::AcpiParser> result = acpi_lite::AcpiParserInit(acpi_rsdp);
    if (result.is_error()) {
      panic("Could not initialize ACPI. Error code: %d.", result.error_value());
    }
    return result.value();
  }();
  global_acpi_parser = &parser;
}

STATIC_COMMAND_START
STATIC_COMMAND("acpidump", "dump ACPI tables to console", &ConsoleAcpiDump)
STATIC_COMMAND_END(vm)
