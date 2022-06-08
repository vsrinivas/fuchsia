// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_

#ifdef __Fuchsia__
#include <bind/fuchsia/acpi/cpp/bind.h>
#endif

namespace acpi {
#ifdef __Fuchsia__
enum BusType {
  kUnknown = bind_fuchsia_acpi::BIND_ACPI_BUS_TYPE_UNKNOWN,
  kPci = bind_fuchsia_acpi::BIND_ACPI_BUS_TYPE_PCI,
  kSpi = bind_fuchsia_acpi::BIND_ACPI_BUS_TYPE_SPI,
  kI2c = bind_fuchsia_acpi::BIND_ACPI_BUS_TYPE_I2C,
};
#else
enum BusType {
  kUnknown = 0,
  kPci,
  kSpi,
  kI2c,
};
#endif  // __Fuchsia__
}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_BUS_TYPE_H_
