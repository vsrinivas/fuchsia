// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power.h"

#include <zircon/syscalls/system.h>

#include <acpica/acpi.h>
#include <ddk/debug.h>

void poweroff(void) {
  ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
  if (status == AE_OK) {
    AcpiEnterSleepState(5);
  }
}

void reboot(void) {
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT, NULL);
  if (status != ZX_OK)
    zxlogf(ERROR, "acpi: Failed to enter reboot: %d", status);
  AcpiReset();
}

void reboot_bootloader(void) {
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status =
      zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER, NULL);
  if (status != ZX_OK)
    zxlogf(ERROR, "acpi: Failed to enter bootloader reboot: %d", status);
  AcpiReset();
}

void reboot_recovery(void) {
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status =
      zx_system_powerctl(get_root_resource(), ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY, NULL);
  if (status != ZX_OK)
    zxlogf(ERROR, "acpi: Failed to enter recovery reboot: %d", status);
  AcpiReset();
}

zx_status_t suspend_to_ram(void) { return ZX_ERR_NOT_SUPPORTED; }
