// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE fil

#ifndef ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_
#define ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_

// clang-format off
#define SDM_FRACTIONALITY         ((uint32_t)16384)
#define S905D2_FIXED_PLL_RATE     ((uint32_t)2000000000)

/* Initial configuration values for PLLs.  These were taken
    from the AmLogic SDK.  No documentation available to
    understand settings at this time beyond the multiply/divide
    ratios.
*/
#define G12A_MPLL_CNTL0 0x00000543
#define G12A_MPLL_CNTL2 0x40000033

// TODO(hollande) - the pll design for HIFI and SYS are
//  the same.  Find out from Amlogic whey the register
//  settings are different.
#define G12A_HIFI_PLL_CNTL1 0x00000000
#define G12A_HIFI_PLL_CNTL2 0x00000000
#define G12A_HIFI_PLL_CNTL3 0x6a285c00
#define G12A_HIFI_PLL_CNTL4 0x65771290
#define G12A_HIFI_PLL_CNTL5 0x39272000
#define G12A_HIFI_PLL_CNTL6 0x56540000

#define G12A_SYS_PLL_CNTL1 0x00000000
#define G12A_SYS_PLL_CNTL2 0x00000000
#define G12A_SYS_PLL_CNTL3 0x48681c00
#define G12A_SYS_PLL_CNTL4 0x88770290
#define G12A_SYS_PLL_CNTL5 0x39272000
#define G12A_SYS_PLL_CNTL6 0x56540000

#define G12A_GP0_PLL_CNTL1 0x00000000
#define G12A_GP0_PLL_CNTL2 0x00000000
#define G12A_GP0_PLL_CNTL3 0x48681c00
#define G12A_GP0_PLL_CNTL4 0x33771290
#define G12A_GP0_PLL_CNTL5 0x39272000
#define G12A_GP0_PLL_CNTL6 0x56540000

#define G12A_SYS1_PLL_CNTL1 0x00000000
#define G12A_SYS1_PLL_CNTL2 0x00000000
#define G12A_SYS1_PLL_CNTL3 0x48681c00
#define G12A_SYS1_PLL_CNTL4 0x88770290
#define G12A_SYS1_PLL_CNTL5 0x39272000
#define G12A_SYS1_PLL_CNTL6 0x56540000


// HHI register offsets (all are 32-bit registers)
#define HHI_GP0_PLL_CNTL0           (0x10 << 2)
#define HHI_PCIE_PLL_CNTL0          (0x26 << 2)

#define HHI_HIFI_PLL_CNTL0          (0x36 << 2)
#define HHI_HIFI_PLL_CNTL1          (0x37 << 2)
#define HHI_HIFI_PLL_CNTL2          (0x38 << 2)
#define HHI_HIFI_PLL_CNTL3          (0x39 << 2)
#define HHI_HIFI_PLL_CNTL4          (0x3a << 2)
#define HHI_HIFI_PLL_CNTL5          (0x3b << 2)
#define HHI_HIFI_PLL_CNTL6          (0x3c << 2)

#define HHI_MPLL_CNTL0              (0x9e << 2)
#define HHI_MPLL_CNTL1              (0x9f << 2)
#define HHI_MPLL_CNTL2              (0xa0 << 2)
#define HHI_MPLL_CNTL3              (0xa1 << 2)
#define HHI_MPLL_CNTL4              (0xa2 << 2)
#define HHI_MPLL_CNTL5              (0xa3 << 2)
#define HHI_MPLL_CNTL6              (0xa4 << 2)
#define HHI_MPLL_CNTL7              (0xa5 << 2)
#define HHI_MPLL_CNTL8              (0xa6 << 2)

#define HHI_FIX_PLL_CNTL0           (0xa8 << 2)
#define HHI_FIX_PLL_CNTL1           (0xa9 << 2)
#define HHI_FIX_PLL_CNTL2           (0xaa << 2)
#define HHI_FIX_PLL_CNTL3           (0xab << 2)
#define HHI_FIX_PLL_CNTL4           (0xac << 2)
#define HHI_FIX_PLL_CNTL5           (0xad << 2)
#define HHI_FIX_PLL_CNTL6           (0xae << 2)

#define HHI_SYS_PLL_CNTL0           (0xbd << 2)
#define HHI_SYS_PLL_CNTL1           (0xbe << 2)
#define HHI_SYS_PLL_CNTL2           (0xbf << 2)
#define HHI_SYS_PLL_CNTL3           (0xc0 << 2)
#define HHI_SYS_PLL_CNTL4           (0xc1 << 2)
#define HHI_SYS_PLL_CNTL5           (0xc2 << 2)
#define HHI_SYS_PLL_CNTL6           (0xc3 << 2)

#define HHI_SYS1_PLL_CNTL0           (0xe0 << 2)
#define HHI_SYS1_PLL_CNTL1           (0xe1 << 2)
#define HHI_SYS1_PLL_CNTL2           (0xe2 << 2)
#define HHI_SYS1_PLL_CNTL3           (0xe3 << 2)
#define HHI_SYS1_PLL_CNTL4           (0xe4 << 2)
#define HHI_SYS1_PLL_CNTL5           (0xe5 << 2)
#define HHI_SYS1_PLL_CNTL6           (0xe6 << 2)

#define HHI_GP0_PLL_CNTL0           (0x10 << 2)
#define HHI_GP0_PLL_CNTL1           (0x11 << 2)
#define HHI_GP0_PLL_CNTL2           (0x12 << 2)
#define HHI_GP0_PLL_CNTL3           (0x13 << 2)
#define HHI_GP0_PLL_CNTL4           (0x14 << 2)
#define HHI_GP0_PLL_CNTL5           (0x15 << 2)
#define HHI_GP0_PLL_CNTL6           (0x16 << 2)

// HHI PLL register bitfield definitions
#define HHI_PLL_LOCK           (1 << 31)
#define HHI_PLL_CNTL0_EN       (1 << 28)
#define HHI_PLL_CNTL0_RESET    (1 << 29)
#define HHI_PLL_CNTL0_M_SHIFT  (0)
#define HHI_PLL_CNTL0_M        (0xff << HHI_PLL_CNTL0_M_SHIFT)
#define HHI_PLL_CNTL0_N_SHIFT  (10)
#define HHI_PLL_CNTL0_N        (0x1f << HHI_PLL_CNTL0_N_SHIFT)
#define HHI_PLL_CNTL0_OD_SHIFT (16)
#define HHI_PLL_CNTL0_OD       (0x3  << HHI_PLL_CNTL0_OD_SHIFT)

// clang-format on

#endif  // ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_
