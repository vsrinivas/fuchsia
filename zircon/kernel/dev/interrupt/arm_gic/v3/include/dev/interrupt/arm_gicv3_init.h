// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V3_INCLUDE_DEV_INTERRUPT_ARM_GICV3_INIT_H_
#define ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V3_INCLUDE_DEV_INTERRUPT_ARM_GICV3_INIT_H_

#include <zircon/boot/driver-config.h>

// Early and late initialization routines for the driver.
void ArmGicInitEarly(const zbi_dcfg_arm_gic_v3_driver_t& config);
void ArmGicInitLate(const zbi_dcfg_arm_gic_v3_driver_t& config);

#endif  // ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V3_INCLUDE_DEV_INTERRUPT_ARM_GICV3_INIT_H_
