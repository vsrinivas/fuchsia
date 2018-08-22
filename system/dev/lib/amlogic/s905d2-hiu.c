// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#include <soc/aml-s905d2/s905d2-hiu.h>
#include <soc/aml-s905d2/s905d2-hw.h>


zx_status_t s905d2_hiu_init(zx_handle_t bti, aml_hiu_dev_t *device) {

    zx_handle_t resource = get_root_resource();
    zx_status_t status;

    status = io_buffer_init_physical(&device->regs_iobuff, bti, S905D2_HIU_BASE,
                                     S905D2_HIU_LENGTH, resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: io_buffer_init_physical failed %d\n", __func__, status);
        return status;
    }

    device->virt_regs = (zx_vaddr_t)(io_buffer_virt(&device->regs_iobuff));

    return ZX_OK;
}

zx_status_t s905d2_pll_init(aml_hiu_dev_t *device, aml_pll_dev_t* pll_dev, hhi_plls_t pll_num) {
    ZX_DEBUG_ASSERT(device);
    ZX_DEBUG_ASSERT(pll_dev);

    if (pll_num == HIFI_PLL) {
        pll_dev->hiu = device;
        pll_dev->rate_table = s905d2_pll_get_rate_table(HIFI_PLL);
        pll_dev->rate_idx = 0;
        pll_dev->frequency = 0;
        pll_dev->pll_num = pll_num;
        //Disable and reset the pll
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL0, 1 << 29);
        //write config values
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL1, G12A_HIFI_PLL_CNTL1);
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL2, G12A_HIFI_PLL_CNTL2);
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL3, G12A_HIFI_PLL_CNTL3);
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL4, G12A_HIFI_PLL_CNTL4);
        hiu_clk_set_reg(device, HHI_HIFI_PLL_CNTL5, G12A_HIFI_PLL_CNTL5);
        return ZX_OK;
    } else if (pll_num == SYS_PLL) {
        pll_dev->hiu = device;
        pll_dev->rate_table = s905d2_pll_get_rate_table(SYS_PLL);
        pll_dev->rate_idx = 0;
        pll_dev->frequency = 0;
        pll_dev->pll_num = pll_num;
        //Disable and reset the pll
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL0, 1 << 29);
        //write config values
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL1, G12A_SYS_PLL_CNTL1);
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL2, G12A_SYS_PLL_CNTL2);
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL3, G12A_SYS_PLL_CNTL3);
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL4, G12A_SYS_PLL_CNTL4);
        hiu_clk_set_reg(device, HHI_SYS_PLL_CNTL5, G12A_SYS_PLL_CNTL5);
        return ZX_OK;
    }
    //Need to find/add values for GP0 and PCIE plls
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t s905d2_pll_ena(aml_pll_dev_t *pll_dev) {
    uint32_t offs = hiu_get_pll_offs(pll_dev);

    uint32_t reg_val = hiu_clk_get_reg(pll_dev->hiu, offs);
    // Set Enable bit
    reg_val |= HHI_PLL_CNTL0_EN;
    hiu_clk_set_reg(pll_dev->hiu, offs, reg_val);
    // Clear Reset bit
    reg_val &= ~HHI_PLL_CNTL0_RESET;
    hiu_clk_set_reg(pll_dev->hiu, offs, reg_val);

    return ZX_OK;
}

/* Notes:
    -VCO needs to be between 3-6GHz per the datasheet. It appears that if you
      provide values which would result in a VCO outside of this range, it will
      still oscillate, but at unknown (but likely close to target) frequency.
*/
zx_status_t s905d2_pll_set_rate(aml_pll_dev_t *pll_dev, uint64_t freq) {
    const hhi_pll_rate_t* pll_rate;

    zx_status_t status = s905d2_pll_fetch_rate(pll_dev, freq, &pll_rate);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t offs = hiu_get_pll_offs(pll_dev);

    uint32_t ctl0 = hiu_clk_get_reg(pll_dev->hiu, offs);

    ctl0 &= ~HHI_PLL_CNTL0_M;
    ctl0 |= pll_rate->m << HHI_PLL_CNTL0_M_SHIFT;

    ctl0 &= ~HHI_PLL_CNTL0_N;
    ctl0 |= pll_rate->n << HHI_PLL_CNTL0_N_SHIFT;

    ctl0 &= ~HHI_PLL_CNTL0_OD;
    ctl0 |= pll_rate->od << HHI_PLL_CNTL0_OD_SHIFT;

    hiu_clk_set_reg(pll_dev->hiu, offs, ctl0);

    hiu_clk_set_reg(pll_dev->hiu, offs + 4, pll_rate->frac);

    return ZX_OK;
}