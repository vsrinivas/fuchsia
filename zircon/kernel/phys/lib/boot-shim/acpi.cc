// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite/debug_port.h>
#include <lib/boot-shim/acpi.h>
#include <stdio.h>

namespace boot_shim {

void AcpiRsdpItem::Init(const acpi_lite::AcpiParser& parser) { set_payload(parser.rsdp_pa()); }

void AcpiUartItem::Init(const acpi_lite::AcpiParserInterface& parser) {
  set_payload();  // Clear out any old state in case we find nothing below.
  if (auto dbg2 = acpi_lite::GetDebugPort(parser); dbg2.is_ok()) {
    switch (dbg2->type) {
      case acpi_lite::AcpiDebugPortDescriptor::Type::kMmio:
        set_payload(dcfg_simple_t{.mmio_phys = dbg2->address});
        break;
      case acpi_lite::AcpiDebugPortDescriptor::Type::kPio:
        set_payload(dcfg_simple_pio_t{.base = static_cast<uint16_t>(dbg2->address)});
        break;
    }
  }
}

}  // namespace boot_shim
