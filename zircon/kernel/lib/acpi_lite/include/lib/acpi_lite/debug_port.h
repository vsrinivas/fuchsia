// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_DEBUG_PORT_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_DEBUG_PORT_H_

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <zircon/types.h>

namespace acpi_lite {

// Describes a dedicated system debug port suitable for low-level
// debugging and diagnostics.
//
// Currently, we only support a 16550-compatible UART using MMIO or PIO.
struct AcpiDebugPortDescriptor {
  enum class Type { kMmio, kPio };

  Type type;

  // Physical address of the 16550 MMIO registers for Type::kMmio.
  // IO port base for Type::kPio.
  zx_paddr_t address;
  size_t length;
};

// Lookup low-level debug port information.
zx::result<AcpiDebugPortDescriptor> GetDebugPort(const AcpiParserInterface& parser);

// Parse an AcpiDbh2Table ACPI structure.
zx::result<AcpiDebugPortDescriptor> ParseAcpiDbg2Table(const AcpiDbg2Table& debug_table);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_DEBUG_PORT_H_
