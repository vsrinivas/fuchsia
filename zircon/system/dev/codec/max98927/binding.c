// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

extern zx_status_t max98927_bind_hook(void*, zx_device_t*);

static zx_driver_ops_t max98927_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = max98927_bind_hook,
};

ZIRCON_DRIVER_BEGIN(max98927, max98927_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x4d583938), // 'MX98'
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x39323700), // '927\0'
ZIRCON_DRIVER_END(max98927)
