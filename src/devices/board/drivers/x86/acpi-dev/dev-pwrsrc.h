// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_PWRSRC_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_PWRSRC_H_

#include <acpica/acpi.h>
#include <ddk/device.h>

#include "power.h"

namespace acpi_pwrsrc {

// function pointer for testability, used to mock out AcpiEvaluateObject where necessary
typedef ACPI_STATUS (*AcpiObjectEvalFunc)(ACPI_HANDLE, char*, ACPI_OBJECT_LIST*, ACPI_BUFFER*);

typedef struct acpi_pwrsrc_device {
  zx_device_t* zxdev;

  ACPI_HANDLE acpi_handle;

  // event to notify on
  zx_handle_t event;

  power_info_t info;

  mtx_t lock;

  AcpiObjectEvalFunc acpi_eval;

} acpi_pwrsrc_device_t;

zx_status_t call_PSR(acpi_pwrsrc_device_t* dev);

}  // namespace acpi_pwrsrc

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_PWRSRC_H_
