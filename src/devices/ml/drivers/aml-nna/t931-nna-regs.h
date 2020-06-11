// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_
#define SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

// Power domain.
#define AO_RTI_GEN_PWR_SLEEP0 (0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0 (0x3b << 2)

// Memory PD.
#define HHI_NANOQ_MEM_PD_REG0 (0x43 << 2)
#define HHI_NANOQ_MEM_PD_REG1 (0x44 << 2)
#define HHI_VIPNANOQ_CLK_CNTL (0x72 << 2)

#endif  // SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_
