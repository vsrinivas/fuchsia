// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <hw/reg.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-sip.h>
#include <zircon/syscalls/smc.h>

#include "imx8mevk.h"

static const pbus_mmio_t mmios[] = {
    {
        .base = IMX8M_GPU_BASE,
        .length = IMX8M_GPU_LENGTH,
    }};

static const pbus_irq_t irqs[] = {};

static const pbus_bti_t btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_GPU,
    }};

static const pbus_dev_t dev = {
    .name = "vsl-gc",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_GPU_VSL_GC,
    .mmio_list = mmios,
    .mmio_count = countof(mmios),
    .irq_list = irqs,
    .irq_count = countof(irqs),
    .bti_list = btis,
    .bti_count = countof(btis),
};

enum {
    DIVIDE_BY_ONE = 0,
    DIVIDE_BY_TWO = 1,
};

enum {
    ENABLE = 1,
};

static zx_status_t clock_gating_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    const uint32_t kOffset = 0x4570;
    writel(0x3, ccm_regs + kOffset);
    return ZX_OK;
}

static zx_status_t core_clock_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    enum {
        IMX8_25M_REF_CLK = 0,
        IMX8_GPU_PLL_CLK = 1,
        IMX8_SYSTEM_PLL1_CLK = 2,
        IMX8_SYSTEM_PLL3_CLK = 3,
        IMX8_SYSTEM_PLL2_CLK = 4,
        IMX8_AUDIO_PLL1_CLK = 5,
        IMX8_VIDEO_PLL1_CLK = 6,
        IMX8_AUDIO_PLL2_CLK = 7,
    };

    const uint32_t kOffset = 0x8180;
    uint32_t reg_val = 0;
    reg_val |= (ENABLE << 28);           // enable: 1 bit
    reg_val |= (IMX8_GPU_PLL_CLK << 24); // mux: 3 bits.
    reg_val |= (DIVIDE_BY_ONE << 0);     // divider: 4 bits

    writel(reg_val, ccm_regs + kOffset);

    return ZX_OK;
}

static zx_status_t shader_clock_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    enum {
        IMX8_25M_REF_CLK = 0,
        IMX8_GPU_PLL_CLK = 1,
        IMX8_SYSTEM_PLL1_CLK = 2,
        IMX8_SYSTEM_PLL3_CLK = 3,
        IMX8_SYSTEM_PLL2_CLK = 4,
        IMX8_AUDIO_PLL1_CLK = 5,
        IMX8_VIDEO_PLL1_CLK = 6,
        IMX8_AUDIO_PLL2_CLK = 7,
    };

    const uint32_t kOffset = 0x8200;
    uint32_t reg_val = 0;
    reg_val |= (ENABLE << 28);           // enable: 1 bit
    reg_val |= (IMX8_GPU_PLL_CLK << 24); // mux: 3 bits.
    reg_val |= (DIVIDE_BY_ONE << 0);     // divider: 4 bits

    writel(reg_val, ccm_regs + kOffset);
    printf("%s: wrote reg_val 0x%x\n", __FUNCTION__, reg_val);

    return ZX_OK;
}

static zx_status_t axi_clock_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    enum {
        IMX8_25M_REF_CLK = 0,
        IMX8_SYSTEM_PLL1_CLK = 1,
        IMX8_GPU_PLL_CLK = 2,
        IMX8_SYSTEM_PLL3_CLK = 3,
        IMX8_SYSTEM_PLL2_CLK = 4,
        IMX8_AUDIO_PLL1_CLK = 5,
        IMX8_VIDEO_PLL1_CLK = 6,
        IMX8_AUDIO_PLL2_CLK = 7,
    };

    const uint32_t kOffset = 0x8c00;
    uint32_t reg_val = 0;
    reg_val |= (ENABLE << 28);           // enable: 1 bit
    reg_val |= (IMX8_GPU_PLL_CLK << 24); // mux: 3 bits.
    reg_val |= (DIVIDE_BY_ONE << 0);     // divider: 4 bits

    writel(reg_val, ccm_regs + kOffset);

    return ZX_OK;
}

static zx_status_t ahb_clock_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    enum {
        IMX8_25M_REF_CLK = 0,
        IMX8_SYSTEM_PLL1_CLK = 1,
        IMX8_GPU_PLL_CLK = 2,
        IMX8_SYSTEM_PLL3_CLK = 3,
        IMX8_SYSTEM_PLL2_CLK = 4,
        IMX8_AUDIO_PLL1_CLK = 5,
        IMX8_VIDEO_PLL1_CLK = 6,
        IMX8_AUDIO_PLL2_CLK = 7,
    };

    const uint32_t kOffset = 0x8c80;
    uint32_t reg_val = 0;
    reg_val |= (ENABLE << 28);           // enable: 1 bit
    reg_val |= (IMX8_GPU_PLL_CLK << 24); // mux: 3 bits.
    reg_val |= (DIVIDE_BY_TWO << 0);     // divider: 4 bits

    writel(reg_val, ccm_regs + kOffset);

    return ZX_OK;
}

static zx_status_t clock_init(imx8mevk_bus_t* bus, volatile uint8_t* ccm_regs) {
    zx_status_t status;

    status = core_clock_init(bus, ccm_regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: core_clock_init failed: %d\n", __FUNCTION__, status);
        return status;
    }

    status = shader_clock_init(bus, ccm_regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: shader_clock_init failed: %d\n", __FUNCTION__, status);
        return status;
    }

    status = axi_clock_init(bus, ccm_regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: axi_clock_init failed: %d\n", __FUNCTION__, status);
        return status;
    }

    status = ahb_clock_init(bus, ccm_regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ahb_clock_init failed: %d\n", __FUNCTION__, status);
        return status;
    }

    status = clock_gating_init(bus, ccm_regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: clock_gating_init failed: %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

zx_status_t imx_gpu_init(imx8mevk_bus_t* bus) {
    // Enable power domain.
    zx_smc_parameters_t smc_params = {.func_id = IMX8M_SIP_GPC,
                                      .arg1 = IMX8M_SIP_CONFIG_GPC_PM_DOMAIN,
                                      .arg2 = IMX8M_PD_GPU,
                                      .arg3 = 1};
    zx_smc_result_t smc_result;
    zx_status_t status = zx_smc_call(get_root_resource(), &smc_params, &smc_result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: SMC power on failed %d\n", __FUNCTION__, status);
        return status;
    }

    // Map Clock Control Module.
    mmio_buffer_t ccm_buffer;
    status = mmio_buffer_init_physical(&ccm_buffer, IMX8M_AIPS_CCM_BASE, IMX8M_AIPS_LENGTH,
                                       get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to init ccm buffer: %d\n", __FUNCTION__, status);
        return status;
    }

    status = clock_init(bus, ccm_buffer.vaddr);

    mmio_buffer_release(&ccm_buffer);

    if (status != ZX_OK) {
        return status;
    }

    status = pbus_device_add(&bus->pbus, &dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "pbus_device_add failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
