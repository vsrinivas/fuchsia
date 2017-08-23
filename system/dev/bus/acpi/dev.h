// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <acpica/acpi.h>
#include <ddk/device.h>

#define BATTERY_HID_STRING "PNP0C0A"
#define EC_HID_STRING      "PNP0C09"
#define PWRSRC_HID_STRING  "ACPI0003"

mx_status_t battery_init(mx_device_t* parent, ACPI_HANDLE acpi_handle);
mx_status_t ec_init(mx_device_t* parent, ACPI_HANDLE acpi_handle);
mx_status_t pwrsrc_init(mx_device_t* parent, ACPI_HANDLE acpi_handle);
