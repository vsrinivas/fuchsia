// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <ddk/mmio-buffer.h>
#include <hw/reg.h>
#include <zircon/assert.h>

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

typedef enum {
    GP0_PLL,
    PCIE_PLL,
    HIFI_PLL,
    SYS_PLL,
} hhi_plls_t;

typedef struct {
    uint64_t rate;
    uint32_t n;
    uint32_t m;
    uint32_t frac;
    uint32_t od;
} hhi_pll_rate_t;

typedef struct aml_hiu_dev {
    mmio_buffer_t mmio;
    uint8_t* regs_vaddr;
} aml_hiu_dev_t;

typedef struct aml_pll_dev {
    aml_hiu_dev_t* hiu;               // Pointer to the register control block.
    const hhi_pll_rate_t* rate_table; // Pointer to this PLLs rate table.
    uint32_t rate_idx;                // Index in rate table of current setting.
    uint32_t frequency;               // Current operating frequency.
    hhi_plls_t pll_num;               // Which pll is this
    size_t rate_count;                // Number of entries in the rate table.
} aml_pll_dev_t;

static inline uint32_t hiu_clk_get_reg(aml_hiu_dev_t* dev, uint32_t offset) {
    return readl(dev->regs_vaddr + offset);
}

static inline uint32_t hiu_clk_set_reg(aml_hiu_dev_t* dev, uint32_t offset, uint32_t value) {
    writel(value, dev->regs_vaddr + offset);
    return hiu_clk_get_reg(dev, offset);
}

static inline uint32_t hiu_get_pll_offs(aml_pll_dev_t* pll_dev) {
    switch (pll_dev->pll_num) {
    case GP0_PLL:
        return HHI_GP0_PLL_CNTL0;
    case PCIE_PLL:
        return HHI_PCIE_PLL_CNTL0;
    case HIFI_PLL:
        return HHI_HIFI_PLL_CNTL0;
    case SYS_PLL:
        return HHI_SYS_PLL_CNTL0;
    default:
        ZX_DEBUG_ASSERT(0);
    }
    return 0;
}

__BEGIN_CDECLS

/*
    Maps the hiu register block (containing all the pll controls).
*/
zx_status_t s905d2_hiu_init(aml_hiu_dev_t* device);

/*
    Initializes the selected pll. This resetting the pll and writing initial
    values to control registers.  When exiting init the PLL will be in a
    halted (de-enabled) state.
*/
zx_status_t s905d2_pll_init(aml_hiu_dev_t* device, aml_pll_dev_t* pll, hhi_plls_t pll_num);
/*
    Sets the rate of the selected pll. If the requested frequency is not found
    it will return with ZX_ERR_NOT_SUPPORTED.
*/
zx_status_t s905d2_pll_set_rate(aml_pll_dev_t* pll, uint64_t freq);

/*
    Enables the selected pll.  This assumes the pll has been initialized and
    valid divider values have been written to the control registers.
*/
zx_status_t s905d2_pll_ena(aml_pll_dev_t* pll);

/*
    Disable the selected pll.  Returns if the pll was actually enabled
    when the call was made.
*/
bool s905d2_pll_disable(aml_pll_dev_t* pll_dev);

/*
    Look for freq in pll rate table.  Returns ZX_ERR_NOT_SUPPORTED if the rate
    can not be found.
*/
zx_status_t s905d2_pll_fetch_rate(aml_pll_dev_t* pll_dev, uint64_t freq, const hhi_pll_rate_t** pll_rate);

/*
    Returns correct pll rate table for selected pll.
*/
const hhi_pll_rate_t* s905d2_pll_get_rate_table(hhi_plls_t pll_num);

/*
    Returns the count of the rate table for the pll.
*/
size_t s905d2_get_rate_table_count(hhi_plls_t pll_num);

__END_CDECLS
