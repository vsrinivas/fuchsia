// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/syscalls.h>

#include <hw/reg.h>
#include <soc/hi3660/hi3660-regs.h>

#include "hikey960.h"

#define BIT(pos) (1U << (pos))
#define set_bits(v, a) writel(readl(a) | (v), (a))
#define clr_bits(v, a) writel(readl(a) & (uint32_t) ~(v), (a))

zx_status_t hikey960_ufs_clock_init(hikey960_t* hikey) {
  volatile void* ufs_sctrl = hikey->ufs_sctrl.vaddr;
  volatile void* peri_crg = hikey->peri_crg.vaddr;
  volatile void* pctrl = hikey->pctrl.vaddr;

  writel(PERI_CRG_UFS_IO, peri_crg + PERI_CRG_UFS_ISODIS);

  clr_bits(UFS_SCTRL_REF_CLK_EN, (ufs_sctrl + UFS_SCTRL_PHY_CLK_CTRL));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  /* Use ABB clock */
  clr_bits(UFS_SCTRL_REF_CLK_SRC_SEl, (ufs_sctrl + UFS_SCTRL_SYSCTRL));
  clr_bits(UFS_SCTRL_REF_CLK_ISO_EN, (ufs_sctrl + UFS_SCTRL_PHY_ISO_EN));
  writel(BIT(0) | BIT(16), pctrl + PCTRL_CTRL3);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  /* Open device Ref clock */
  writel(BIT(14), peri_crg + PERI_CRG_UFS_ISOEN);

  /* Open MPHY Ref clock */
  set_bits(UFS_SCTRL_REF_CLK_EN, (ufs_sctrl + UFS_SCTRL_PHY_CLK_CTRL));

  return ZX_OK;
}

zx_status_t hikey960_ufs_soc_init(hikey960_t* hikey) {
  volatile void* ufs_sctrl = hikey->ufs_sctrl.vaddr;
  volatile void* peri_crg = hikey->peri_crg.vaddr;
  uint32_t val;

  /* HC reset_n enable */
  writel(PERI_CRG_UFS_RST, peri_crg + PERI_CRG_UFS_RSTEN3);

  /* HC PSW powerup */
  set_bits(UFS_SCTRL_PSW_MTCMOS_EN, (ufs_sctrl + UFS_SCTRL_PSW_PWR_CTRL));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  /* notify PWR ready */
  set_bits(UFS_SCTRL_PWR_READY, (ufs_sctrl + UFS_SCTRL_HC_LP_CTRL));

  /* Enable device reset */
  writel(UFS_SCTRL_MASK_DEV_RST | 0, ufs_sctrl + UFS_SCTRL_DEV_RST_CTRL);

  /* set HC hclk div */
  writel(BIT(14 + 16) | 0, peri_crg + PERI_CRG_UFS_CLKDIV17);

  /* set HC hclk div */
  writel((0x3 << 9) | (0x3 << (9 + 16)), peri_crg + PERI_CRG_UFS_CLKDIV16);

  /* set cfg clk freq */
  val = readl(ufs_sctrl + UFS_SCTRL_PHY_CLK_CTRL);
  val = val & (~UFS_SCTRL_CLK_FREQ_MASK);
  val = val | UFS_SCTRL_CLK_FREQ_CFG;
  writel(val, ufs_sctrl + UFS_SCTRL_PHY_CLK_CTRL);

  /* set  ref clk freq */
  clr_bits(UFS_SCTRL_REF_CLK_SEL_MASK, (ufs_sctrl + UFS_SCTRL_PHY_CLK_CTRL));

  /* bypass ufs clk gate */
  set_bits(UFS_SCTRL_CLK_GATE_BYPASS_MASK, (ufs_sctrl + UFS_SCTRL_CLK_GATE_BYPASS));
  set_bits(UFS_SCTRL_SYSCTRL_BYPASS_MASK, (ufs_sctrl + UFS_SCTRL_SYSCTRL));

  /* open psw clk */
  set_bits(UFS_SCTRL_PSW_CLK_EN, (ufs_sctrl + UFS_SCTRL_PSW_CLK_CTRL));

  /* disable ufshc iso */
  clr_bits(UFS_SCTRL_PSW_ISO_CTRL, (ufs_sctrl + UFS_SCTRL_PSW_PWR_CTRL));

  /* disable phy iso */
  clr_bits(UFS_SCTRL_PHY_ISO_CTRL, (ufs_sctrl + UFS_SCTRL_PHY_ISO_EN));

  /* notice iso disable */
  clr_bits(UFS_SCTRL_LP_ISOL_EN, (ufs_sctrl + UFS_SCTRL_HC_LP_CTRL));

  /* disable areset_n */
  writel(PERI_CRG_UFS_ARST, peri_crg + PERI_CRG_UFS_RSTDIS3);

  /* disable lp_reset_n */
  set_bits(UFS_SCTRL_LP_RSTN, (ufs_sctrl + UFS_SCTRL_RST_CTRL_EN));
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  /* Reset device */
  writel(UFS_SCTRL_MASK_UFS_RST | UFS_SCTRL_MASK_DEV_RST, ufs_sctrl + UFS_SCTRL_DEV_RST_CTRL);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

  /*
   * enable the fix of linereset recovery and enable rx_reset/tx_rest beat
   * enable ref_clk_en override(bit5) & override value = 1(bit4), with mask
   */
  writel(0x03300330, ufs_sctrl + UFS_SCTRL_DEV_RST_CTRL);

  writel(PERI_CRG_UFS_RST, peri_crg + PERI_CRG_UFS_RSTDIS3);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

  return ZX_OK;
}

zx_status_t hikey960_ufs_init(hikey960_t* hikey) {
  zx_status_t status;

  status = hikey960_ufs_clock_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_ufs_soc_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  return ZX_OK;

fail:
  return status;
}
