// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "aml.h"

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S905D2_MALI_BASE,
        .length = S905D2_MALI_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S905D2_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D2_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D2_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_MALI,
    },
};

static const pbus_dev_t mali_dev = {
    .name = "mali",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_ARM_MALI,
    .mmios = mali_mmios,
    .mmio_count = countof(mali_mmios),
    .irqs = mali_irqs,
    .irq_count = countof(mali_irqs),
    .btis = mali_btis,
    .bti_count = countof(mali_btis),
};

#define HHI_MALI_CLK_CNTL (0x6c << 2)

zx_status_t aml_mali_init(aml_bus_t* bus) {
    zx_status_t status = ZX_OK;

    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init: iommu_get_bti failed: %d\n", status);
        return status;
    }

    io_buffer_t hiu_buffer;
    status = io_buffer_init_physical(&hiu_buffer, bti, S905D2_HIU_BASE, S905D2_HIU_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init io_buffer_init_physical hiu failed %d\n", status);
        goto fail1;
    }

    io_buffer_t preset_buffer;
    status = io_buffer_init_physical(&preset_buffer, bti, S905D2_RESET_BASE, S905D2_RESET_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init io_buffer_init_physical preset failed %d\n", status);
        goto fail2;
    }

    io_buffer_t gpu_buffer;
    status = io_buffer_init_physical(&gpu_buffer, bti, S905D2_MALI_BASE, S905D2_MALI_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init io_buffer_init_physical gpu failed %d\n", status);
        goto fail3;
    }

    volatile uint8_t* hiu_regs = io_buffer_virt(&hiu_buffer);
    volatile uint8_t* preset_regs = io_buffer_virt(&preset_buffer);
    volatile uint8_t* gpu_regs = io_buffer_virt(&gpu_buffer);

    uint32_t temp;

    temp = readl(preset_regs + S905D2_RESET0_MASK);
    temp &= ~(1 << 20);
    writel(temp, preset_regs + S905D2_RESET0_MASK);

    temp = readl(preset_regs + S905D2_RESET0_LEVEL);
    temp &= ~(1 << 20);
    writel(temp, preset_regs + S905D2_RESET0_LEVEL);

    temp = readl(preset_regs + S905D2_RESET2_MASK);
    temp &= ~(1 << 14);
    writel(temp, preset_regs + S905D2_RESET2_MASK);

    temp = readl(preset_regs + S905D2_RESET2_LEVEL);
    temp &= ~(1 << 14);
    writel(temp, preset_regs + S905D2_RESET2_LEVEL);

    enum {
        XTAL = 0, // 24MHz
        GP0 = 1,
        HIFI = 2,
        FCLK_DIV2P5 = 3, // 800 MHz
        FCLK_DIV3 = 4,   // 666 MHz
        FCLK_DIV4 = 5,   // 500 MHz
        FCLK_DIV5 = 6,   // 400 MHz
        FCLK_DIV7 = 7,   // 285.7 MHz
    };
#define CALCULATE_CLOCK(enabled, base, divisor) \
    ((!!(enabled) << 8) | (base << 9) | (divisor - 1))

    enum {
        MHZ500 = CALCULATE_CLOCK(true, FCLK_DIV4, 1)
    };

    writel(MHZ500, hiu_regs + HHI_MALI_CLK_CNTL);
    zx_nanosleep(zx_deadline_after(ZX_USEC(500)));

    temp = readl(preset_regs + S905D2_RESET0_LEVEL);
    temp |= 1 << 20;
    writel(temp, preset_regs + S905D2_RESET0_LEVEL);

    temp = readl(preset_regs + S905D2_RESET2_LEVEL);
    temp |= 1 << 14;
    writel(temp, preset_regs + S905D2_RESET2_LEVEL);

#define PWR_KEY 0x50
#define PWR_OVERRIDE1 0x58

    writel(0x2968A819, gpu_regs + PWR_KEY);
    writel(0xfff | (0x20 << 16), gpu_regs + PWR_OVERRIDE1);

    if ((status = pbus_device_add(&bus->pbus, &mali_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init could not add mali_dev: %d\n", status);
    }

    io_buffer_release(&hiu_buffer);
fail3:
    io_buffer_release(&preset_buffer);
fail2:
    io_buffer_release(&gpu_buffer);
fail1:
    zx_handle_close(bti);

    return status;
}
