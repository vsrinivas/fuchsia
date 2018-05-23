// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660.h>
#include <soc/hi3660/hi3660-regs.h>

#include <stdio.h>

zx_status_t hi3660_dsi_init(hi3660_t* hi3660) {
    volatile void* peri_crg = io_buffer_virt(&hi3660->peri_crg);
    uint32_t temp;

    writel(0x30000000, peri_crg + PERRSTDIS3);

    temp = readl(peri_crg + TXDPHY0_REF_OFFSET);
    temp |= (1 << TXDPHY0_REF_BIT);
    writel(temp, peri_crg + TXDPHY0_REF_OFFSET);
    readl(peri_crg + TXDPHY0_REF_OFFSET + CLKGATE_SEPERATED_STATUS);

    temp = readl(peri_crg + TXDPHY0_CFG_OFFSET);
    temp |= (1 << TXDPHY0_CFG_BIT);
    writel(temp, peri_crg + TXDPHY0_CFG_OFFSET);
    readl(peri_crg + TXDPHY0_CFG_OFFSET + CLKGATE_SEPERATED_STATUS);

    temp = readl(peri_crg + PCLK_GATE_DSI0_OFFSET);
    temp |= (1 << PCLK_GATE_DSI0_BIT);
    writel(temp, peri_crg + PCLK_GATE_DSI0_OFFSET);
    readl(peri_crg + PCLK_GATE_DSI0_OFFSET + CLKGATE_SEPERATED_STATUS);

    return ZX_OK;
}