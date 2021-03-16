// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_BATTERY_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_BATTERY_H_

#include <lib/ddk/device.h>

#include <atomic>

#include <acpica/acpi.h>

#include "power.h"

namespace acpi_battery {

// function pointer for testability, used to mock out AcpiEvaluateObject where necessary
typedef ACPI_STATUS (*AcpiObjectEvalFunc)(ACPI_HANDLE, char*, ACPI_OBJECT_LIST*, ACPI_BUFFER*);

// constant used for rate-limiting ACPI event notifications due to some EC
// implementations that can enter an infinite loop by triggering notifications
// as a result of ACPI BST object evaluation.
static const uint8_t ACPI_EVENT_NOTIFY_LIMIT_MS = 10;

typedef struct acpi_battery_device {
  zx_device_t* zxdev;

  ACPI_HANDLE acpi_handle;

  ACPI_BUFFER bst_buffer;
  ACPI_BUFFER bif_buffer;

  mtx_t lock;

  // event to notify on
  zx_handle_t event;
  zx_time_t last_notify_timestamp;

  power_info_t power_info;
  battery_info_t battery_info;

  std::atomic_bool shutdown;

  AcpiObjectEvalFunc acpi_eval;
} acpi_battery_device_t;

zx_status_t call_BST(acpi_battery_device_t* dev);
zx_status_t call_STA(acpi_battery_device_t* dev);

}  // namespace acpi_battery

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_DEV_DEV_BATTERY_H_
