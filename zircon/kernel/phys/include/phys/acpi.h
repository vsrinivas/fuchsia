// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ACPI_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ACPI_H_

#include <lib/acpi_lite.h>
#include <lib/zx/result.h>
#include <stdint.h>

// Returns a new AcpiParser instance, that uses a physical memory reader, that the transformation
// from physical address to virtual address is identity.
zx::result<acpi_lite::AcpiParser> MakeAcpiParser(uint64_t acpi_rsdp);

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ACPI_H_
