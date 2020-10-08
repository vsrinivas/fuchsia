// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_PLATFORM_PC_ACPI_H_
#define ZIRCON_KERNEL_PLATFORM_PC_ACPI_H_

#include <zircon/types.h>

// Set up an ACPI for the platform.
//
// Panic on failure.
void PlatformInitAcpi(zx_paddr_t acpi_rsdp);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_ACPI_H_
