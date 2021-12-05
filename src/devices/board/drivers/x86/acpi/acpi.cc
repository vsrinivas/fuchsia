// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <vector>

namespace acpi {

acpi::status<uint8_t> Acpi::CallBbn(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "_BBN", std::nullopt);
  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }

  if (ret->Integer.Value > std::numeric_limits<uint8_t>::max()) {
    return acpi::error(AE_BAD_VALUE);
  }

  return acpi::ok(static_cast<uint8_t>(ret->Integer.Value));
}

acpi::status<uint16_t> Acpi::CallSeg(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "SEG", std::nullopt);

  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }

  // Lower 8 bits of _SEG returned integer is the PCI segment group.
  return acpi::ok(static_cast<uint16_t>(ret->Integer.Value & 0xff));
}

}  // namespace acpi
