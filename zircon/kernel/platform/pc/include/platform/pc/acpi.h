// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_ACPI_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_ACPI_H_

#include <lib/acpi_lite.h>
#include <zircon/types.h>

// Set up an ACPI for the platform.
//
// Panic on failure.
void PlatformInitAcpi(zx_paddr_t acpi_rsdp);

// Get global acpi_lite instance.
acpi_lite::AcpiParser& GlobalAcpiLiteParser();

// Suspend the platform to the |target_s_state| sleep state.
//
// This function sets up the waking vector that will be used on resume, then disables interrupts so
// that the platform and architecture hooks to prepare the system for suspend can be called, then
// makes the sleep state transition. Before returning, this function calls on the architecture and
// platform resume hooks and restores the interrupt state.
//
// This function returns ZX_OK after a successful resume, and ZX_ERR_INTERNAL otherwise.
//
// This function must be called with secondary CPUs disabled.
zx_status_t PlatformSuspend(uint8_t target_s_state, uint8_t sleep_type_a, uint8_t sleep_type_b);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_ACPI_H_
