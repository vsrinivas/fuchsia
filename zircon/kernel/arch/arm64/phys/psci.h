// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_PHYS_PSCI_H_
#define ZIRCON_KERNEL_ARCH_ARM64_PHYS_PSCI_H_

#include <zircon/boot/driver-config.h>

void ArmPsciSetup(const dcfg_arm_psci_driver_t* cfg);

// This is defined in assembly.
extern "C" [[noreturn]] void ArmPsciReset();

#endif  // ZIRCON_KERNEL_ARCH_ARM64_PHYS_PSCI_H_
