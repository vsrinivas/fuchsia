// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/acpi-arm64/acpi-arm64.h"

#include <lib/ddk/debug.h>

#include "src/devices/board/drivers/acpi-arm64/acpi-arm64-bind.h"

namespace acpi_arm64 {

zx_status_t AcpiArm64::Create(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "Hello world!");
  return ZX_ERR_NOT_SUPPORTED;
}

static constexpr zx_driver_ops_t driver_ops{
    .version = DRIVER_OPS_VERSION,
    .bind = AcpiArm64::Create,
};

}  // namespace acpi_arm64

ZIRCON_DRIVER(acpi_arm64, acpi_arm64::driver_ops, "zircon", "0.1");
