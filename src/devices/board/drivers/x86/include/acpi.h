// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_H_

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/device.h>
#include <zircon/compiler.h>

zx_status_t publish_acpi_devices(zx_device_t* parent, zx_device_t* sys_root,
                                 zx_device_t* acpi_root);
zx_status_t acpi_suspend(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason,
                         uint8_t* out_state);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_ACPI_H_
