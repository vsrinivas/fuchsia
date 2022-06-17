// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_POWER_RESOURCE_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_POWER_RESOURCE_H_

#include <zircon/types.h>

#include <cstdint>
#include <unordered_set>

#include "src/devices/board/lib/acpi/acpi.h"
#include "third_party/acpica/source/include/actypes.h"

namespace acpi {

class PowerResource {
 public:
  explicit PowerResource(Acpi* acpi, ACPI_HANDLE handle) : acpi_(acpi), acpi_handle_(handle) {}

  virtual ~PowerResource() = default;

  zx_status_t Init();

  uint8_t system_level() const { return system_level_; }
  uint16_t resource_order() const { return resource_order_; }

 private:
  Acpi* acpi_;
  ACPI_HANDLE acpi_handle_;
  uint8_t system_level_;
  uint16_t resource_order_;
  bool is_on_ = false;
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_POWER_RESOURCE_H_
