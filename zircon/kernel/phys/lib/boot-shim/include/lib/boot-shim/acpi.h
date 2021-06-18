// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ACPI_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ACPI_H_

#include <zircon/boot/driver-config.h>

#include "item-base.h"

// Forward declaration for <lib/acpi_lite.h>.
namespace acpi_lite {
class AcpiParserInterface;
}  // namespace acpi_lite

namespace boot_shim {

// This can supply a ZBI_TYPE_KERNEL_DRIVER item based on the serial console
// details in ACPI's DBG2 table.
class AcpiUartItem
    : public boot_shim::SingleVariantItemBase<AcpiUartItem, dcfg_simple_t, dcfg_simple_pio_t> {
 public:
  // This initializes the data from ACPI tables.
  void Init(const acpi_lite::AcpiParserInterface& parser);

  static constexpr zbi_header_t ItemHeader(const dcfg_simple_t& cfg) {
    return {.type = ZBI_TYPE_KERNEL_DRIVER, .extra = KDRV_I8250_MMIO_UART};
  }

  static constexpr zbi_header_t ItemHeader(const dcfg_simple_pio_t& cfg) {
    return {.type = ZBI_TYPE_KERNEL_DRIVER, .extra = KDRV_I8250_PIO_UART};
  }
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ACPI_H_
