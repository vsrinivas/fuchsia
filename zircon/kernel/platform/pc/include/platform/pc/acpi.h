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

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_ACPI_H_
