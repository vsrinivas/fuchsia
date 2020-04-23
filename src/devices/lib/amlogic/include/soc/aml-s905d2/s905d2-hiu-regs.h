// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE fil

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_

// clang-format off
#define SDM_FRACTIONALITY         (UINT32_C(16384))
#define S905D2_FIXED_PLL_RATE     (UINT32_C(2000000000))

/* Initial configuration values for PLLs.  These were taken
    from the AmLogic SDK.  No documentation available to
    understand settings at this time beyond the multiply/divide
    ratios.
*/
#define G12A_MPLL_CNTL0 UINT32_C(0x00000543)
#define G12A_MPLL_CNTL2 UINT32_C(0x40000033)

// TODO(hollande) - the pll design for HIFI and SYS are
//  the same.  Find out from Amlogic whey the register
//  settings are different.
#define G12A_HIFI_PLL_CNTL1 UINT32_C(0x00000000)
#define G12A_HIFI_PLL_CNTL2 UINT32_C(0x00000000)
#define G12A_HIFI_PLL_CNTL3 UINT32_C(0x6a285c00)
#define G12A_HIFI_PLL_CNTL4 UINT32_C(0x65771290)
#define G12A_HIFI_PLL_CNTL5 UINT32_C(0x39272000)
#define G12A_HIFI_PLL_CNTL6 UINT32_C(0x56540000)

#define G12A_SYS_PLL_CNTL1 UINT32_C(0x00000000)
#define G12A_SYS_PLL_CNTL2 UINT32_C(0x00000000)
#define G12A_SYS_PLL_CNTL3 UINT32_C(0x48681c00)
#define G12A_SYS_PLL_CNTL4 UINT32_C(0x88770290)
#define G12A_SYS_PLL_CNTL5 UINT32_C(0x39272000)
#define G12A_SYS_PLL_CNTL6 UINT32_C(0x56540000)

#define G12A_GP0_PLL_CNTL1 UINT32_C(0x00000000)
#define G12A_GP0_PLL_CNTL2 UINT32_C(0x00000000)
#define G12A_GP0_PLL_CNTL3 UINT32_C(0x48681c00)
#define G12A_GP0_PLL_CNTL4 UINT32_C(0x33771290)
#define G12A_GP0_PLL_CNTL5 UINT32_C(0x39272000)
#define G12A_GP0_PLL_CNTL6 UINT32_C(0x56540000)

#define G12A_SYS1_PLL_CNTL1 UINT32_C(0x00000000)
#define G12A_SYS1_PLL_CNTL2 UINT32_C(0x00000000)
#define G12A_SYS1_PLL_CNTL3 UINT32_C(0x48681c00)
#define G12A_SYS1_PLL_CNTL4 UINT32_C(0x88770290)
#define G12A_SYS1_PLL_CNTL5 UINT32_C(0x39272000)
#define G12A_SYS1_PLL_CNTL6 UINT32_C(0x56540000)


// HHI register offsets (all are 32-bit registers)
#define HHI_GP0_PLL_CNTL0           (UINT32_C(0x10) << 2)
#define HHI_PCIE_PLL_CNTL0          (UINT32_C(0x26) << 2)

#define HHI_HIFI_PLL_CNTL0          (UINT32_C(0x36) << 2)
#define HHI_HIFI_PLL_CNTL1          (UINT32_C(0x37) << 2)
#define HHI_HIFI_PLL_CNTL2          (UINT32_C(0x38) << 2)
#define HHI_HIFI_PLL_CNTL3          (UINT32_C(0x39) << 2)
#define HHI_HIFI_PLL_CNTL4          (UINT32_C(0x3a) << 2)
#define HHI_HIFI_PLL_CNTL5          (UINT32_C(0x3b) << 2)
#define HHI_HIFI_PLL_CNTL6          (UINT32_C(0x3c) << 2)

#define HHI_MPLL_CNTL0              (UINT32_C(0x9e) << 2)
#define HHI_MPLL_CNTL1              (UINT32_C(0x9f) << 2)
#define HHI_MPLL_CNTL2              (UINT32_C(0xa0) << 2)
#define HHI_MPLL_CNTL3              (UINT32_C(0xa1) << 2)
#define HHI_MPLL_CNTL4              (UINT32_C(0xa2) << 2)
#define HHI_MPLL_CNTL5              (UINT32_C(0xa3) << 2)
#define HHI_MPLL_CNTL6              (UINT32_C(0xa4) << 2)
#define HHI_MPLL_CNTL7              (UINT32_C(0xa5) << 2)
#define HHI_MPLL_CNTL8              (UINT32_C(0xa6) << 2)

#define HHI_FIX_PLL_CNTL0           (UINT32_C(0xa8) << 2)
#define HHI_FIX_PLL_CNTL1           (UINT32_C(0xa9) << 2)
#define HHI_FIX_PLL_CNTL2           (UINT32_C(0xaa) << 2)
#define HHI_FIX_PLL_CNTL3           (UINT32_C(0xab) << 2)
#define HHI_FIX_PLL_CNTL4           (UINT32_C(0xac) << 2)
#define HHI_FIX_PLL_CNTL5           (UINT32_C(0xad) << 2)
#define HHI_FIX_PLL_CNTL6           (UINT32_C(0xae) << 2)

#define HHI_SYS_PLL_CNTL0           (UINT32_C(0xbd) << 2)
#define HHI_SYS_PLL_CNTL1           (UINT32_C(0xbe) << 2)
#define HHI_SYS_PLL_CNTL2           (UINT32_C(0xbf) << 2)
#define HHI_SYS_PLL_CNTL3           (UINT32_C(0xc0) << 2)
#define HHI_SYS_PLL_CNTL4           (UINT32_C(0xc1) << 2)
#define HHI_SYS_PLL_CNTL5           (UINT32_C(0xc2) << 2)
#define HHI_SYS_PLL_CNTL6           (UINT32_C(0xc3) << 2)

#define HHI_SYS1_PLL_CNTL0          (UINT32_C(0xe0) << 2)
#define HHI_SYS1_PLL_CNTL1          (UINT32_C(0xe1) << 2)
#define HHI_SYS1_PLL_CNTL2          (UINT32_C(0xe2) << 2)
#define HHI_SYS1_PLL_CNTL3          (UINT32_C(0xe3) << 2)
#define HHI_SYS1_PLL_CNTL4          (UINT32_C(0xe4) << 2)
#define HHI_SYS1_PLL_CNTL5          (UINT32_C(0xe5) << 2)
#define HHI_SYS1_PLL_CNTL6          (UINT32_C(0xe6) << 2)

#define HHI_GP0_PLL_CNTL0           (UINT32_C(0x10) << 2)
#define HHI_GP0_PLL_CNTL1           (UINT32_C(0x11) << 2)
#define HHI_GP0_PLL_CNTL2           (UINT32_C(0x12) << 2)
#define HHI_GP0_PLL_CNTL3           (UINT32_C(0x13) << 2)
#define HHI_GP0_PLL_CNTL4           (UINT32_C(0x14) << 2)
#define HHI_GP0_PLL_CNTL5           (UINT32_C(0x15) << 2)
#define HHI_GP0_PLL_CNTL6           (UINT32_C(0x16) << 2)

// HHI PLL register bitfield definitions
#define HHI_PLL_LOCK           (UINT32_C(1) << 31)
#define HHI_PLL_CNTL0_EN       (UINT32_C(1) << 28)
#define HHI_PLL_CNTL0_RESET    (UINT32_C(1) << 29)
#define HHI_PLL_CNTL0_M_SHIFT  (UINT32_C(0))
#define HHI_PLL_CNTL0_M        (UINT32_C(0xff) << HHI_PLL_CNTL0_M_SHIFT)
#define HHI_PLL_CNTL0_N_SHIFT  (UINT32_C(10))
#define HHI_PLL_CNTL0_N        (UINT32_C(0x1f) << HHI_PLL_CNTL0_N_SHIFT)
#define HHI_PLL_CNTL0_OD_SHIFT (UINT32_C(16))
#define HHI_PLL_CNTL0_OD       (UINT32_C(0x3)  << HHI_PLL_CNTL0_OD_SHIFT)

// clang-format on

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_REGS_H_
