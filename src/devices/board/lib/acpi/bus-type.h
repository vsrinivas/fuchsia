// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_

#include <bind/fuchsia/acpi/cpp/fidl.h>

namespace acpi {
enum BusType {
  kUnknown = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_UNKNOWN,
  kPci = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_PCI,
  kSpi = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_SPI,
  kI2c = bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_I2C,
};
}

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_
