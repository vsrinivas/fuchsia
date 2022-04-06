// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_OBJECT_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_OBJECT_H_

#include <acpica/acpi.h>

namespace acpi {

inline ACPI_OBJECT MakeAcpiObject(uint64_t v) {
  return ACPI_OBJECT{
      .Integer =
          {
              .Type = ACPI_TYPE_INTEGER,
              .Value = v,
          },
  };
}

inline ACPI_OBJECT MakeAcpiObject(uint8_t *buf, uint32_t bufsz) {
  return ACPI_OBJECT{
      .Buffer =
          {
              .Type = ACPI_TYPE_BUFFER,
              .Length = bufsz,
              .Pointer = buf,
          },
  };
}

}  // namespace acpi
#endif  // SRC_DEVICES_BOARD_LIB_ACPI_OBJECT_H_
