// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-regs.h>

#include "hikey960.h"

zx_status_t hikey960_dsi_init(hikey960_t* hikey) {
  volatile void* peri_crg = hikey->peri_crg.vaddr;
  uint32_t temp;

  writel(0x30000000, peri_crg + PERRSTDIS3);

  temp = readl(peri_crg + TXDPHY0_REF_OFFSET);
  temp |= (1 << TXDPHY0_REF_BIT);
  writel(temp, peri_crg + TXDPHY0_REF_OFFSET);
  readl(peri_crg + TXDPHY0_REF_OFFSET + CLKGATE_SEPARATED_STATUS);

  temp = readl(peri_crg + TXDPHY0_CFG_OFFSET);
  temp |= (1 << TXDPHY0_CFG_BIT);
  writel(temp, peri_crg + TXDPHY0_CFG_OFFSET);
  readl(peri_crg + TXDPHY0_CFG_OFFSET + CLKGATE_SEPARATED_STATUS);

  temp = readl(peri_crg + PCLK_GATE_DSI0_OFFSET);
  temp |= (1 << PCLK_GATE_DSI0_BIT);
  writel(temp, peri_crg + PCLK_GATE_DSI0_OFFSET);
  readl(peri_crg + PCLK_GATE_DSI0_OFFSET + CLKGATE_SEPARATED_STATUS);

  return ZX_OK;
}
