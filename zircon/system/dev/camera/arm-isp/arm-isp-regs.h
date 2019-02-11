// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

// Power domain.
#define AO_RTI_GEN_PWR_SLEEP0 (0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0 (0x3b << 2)

// Memory PD.
#define HHI_ISP_MEM_PD_REG0 (0x45 << 2)
#define HHI_ISP_MEM_PD_REG1 (0x46 << 2)

// CLK offsets.
#define HHI_CSI_PHY_CNTL0 (0xD3 << 2)
#define HHI_CSI_PHY_CNTL1 (0x114 << 2)

// Reset
#define RESET4_LEVEL 0x90

