// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_THERMAL_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_THERMAL_H_

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <acpica/acpi.h>
#include <ddk/device.h>

namespace acpi_thermal {

typedef struct acpi_thermal_device {
  zx_device_t* zxdev;
  ACPI_HANDLE acpi_handle;

  mtx_t lock;

  // event to notify on
  zx_handle_t event;

  // programmable trip points
  uint32_t trip_point_count;
  bool have_trip[fuchsia_hardware_thermal_MAX_TRIP_POINTS];
  float trip_points[fuchsia_hardware_thermal_MAX_TRIP_POINTS];
} acpi_thermal_device_t;

}  // namespace acpi_thermal

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_THERMAL_H_
