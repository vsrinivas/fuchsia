// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_DEV_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_DEV_H_

#include <lib/ddk/device.h>
#include <zircon/compiler.h>

#include <acpica/acpi.h>

#define BATTERY_HID_STRING "PNP0C0A"
#define LID_HID_STRING "PNP0C0D"
#define EC_HID_STRING "PNP0C09"
#define PWRSRC_HID_STRING "ACPI0003"
#define GOOGLE_CROS_EC_HID_STRING "GOOG0004"
#define GOOGLE_TBMC_HID_STRING "GOOG0006"
#define DPTF_THERMAL_HID_STRING "INT3403"
#define GPE_HID_STRING "ACPI0006"
#define I8042_HID_STRING "PNP0303"
#define RTC_HID_STRING "PNP0B00"
#define I2C_HID_CID_STRING "PNP0C50"
#define GPE_CID_STRING "ACPI0006"
#define GOLDFISH_PIPE_HID_STRING "GFSH0003"
#define GOLDFISH_SYNC_HID_STRING "GFSH0006"
#define SERIAL_HID_STRING "PNP0501"
#define MAX98927_HID_STRING "MX98927"
#define ALC5663_HID_STRING "10EC5663"
#define ALC5514_HID_STRING "10EC5514"

#define HID_LENGTH 8
#define CID_LENGTH 8

zx_status_t battery_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);
zx_status_t ec_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);
zx_status_t pwrbtn_init(zx_device_t* parent);
zx_status_t pwrsrc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);
zx_status_t tbmc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);
zx_status_t cros_ec_lpc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);
zx_status_t thermal_init(zx_device_t* parent, ACPI_DEVICE_INFO* info, ACPI_HANDLE acpi_handle);
zx_status_t lid_init(zx_device_t* parent, ACPI_HANDLE acpi_handle);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_DEV_H_
