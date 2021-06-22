// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_ACPI_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_ACPI_H_

class LegacyBootShim;

void InitAcpi(LegacyBootShim& shim);

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_BOOT_SHIM_ACPI_H_
