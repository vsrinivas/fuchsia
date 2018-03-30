// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S912_MALI_BASE,
        .length = S912_MALI_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S912_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GP,
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

#define RESET0_MASK_REGISTER 0x440
#define RESET0_LEVEL_REGISTER 0x480
#define RESET2_MASK_REGISTER 0x448
#define RESET2_LEVEL_REGISTER 0x488

#define HHI_MALI_CLK_CNTL (0x6c << 2)

zx_status_t vim_mali_init(vim_bus_t* bus) {
    zx_status_t status = ZX_OK;

    io_buffer_t hiu_buffer;
    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_bus_bind: iommu_get_bti failed: %d\n", status);
        return status;
    }

    status = io_buffer_init_physical(&hiu_buffer, bti, S912_HIU_BASE, S912_HIU_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_mali_init io_buffer_init_physical hiu failed %d\n", status);
        goto fail1;
    }

    io_buffer_t preset_buffer;
    status = io_buffer_init_physical(&preset_buffer, bti, S912_PRESET_BASE, S912_PRESET_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_mali_init io_buffer_init_physical preset failed %d\n", status);
        goto fail2;
    }

    io_buffer_t gpu_buffer;
    status = io_buffer_init_physical(&gpu_buffer, bti, S912_MALI_BASE, S912_MALI_LENGTH,
                                     get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_mali_init io_buffer_init_physical gpu failed %d\n", status);
        goto fail3;
    }

    volatile uint8_t* hiu_regs = io_buffer_virt(&hiu_buffer);
    volatile uint8_t* preset_regs = io_buffer_virt(&preset_buffer);
    volatile uint8_t* gpu_regs = io_buffer_virt(&gpu_buffer);

    uint32_t temp;

    temp = readl(preset_regs + RESET0_MASK_REGISTER);
    temp &= ~(1 << 20);
    writel(temp, preset_regs + RESET0_MASK_REGISTER);

    temp = readl(preset_regs + RESET0_LEVEL_REGISTER);
    temp &= ~(1 << 20);
    writel(temp, preset_regs + RESET0_LEVEL_REGISTER);

    temp = readl(preset_regs + RESET2_MASK_REGISTER);
    temp &= ~(1 << 14);
    writel(temp, preset_regs + RESET2_MASK_REGISTER);

    temp = readl(preset_regs + RESET2_LEVEL_REGISTER);
    temp &= ~(1 << 14);
    writel(temp, preset_regs + RESET2_LEVEL_REGISTER);

    enum {
        XTAL = 0, // 25MHz
        GP0 = 1,
        MP2 = 2,
        MP1 = 3,
        FCLK_DIV7 = 4, // 285.7 MHz
        FCLK_DIV4 = 5, // 500 MHz
        FCLK_DIV3 = 6, // 666 MHz
        FCLK_DIV5 = 7, // 400 MHz
    };
#define CALCULATE_CLOCK(enabled, base, divisor) \
    ((!!(enabled) << 8) | (base << 9) | (divisor - 1))

    enum {
        MHZ500 = CALCULATE_CLOCK(true, FCLK_DIV4, 1)
    };

    writel(MHZ500, hiu_regs + HHI_MALI_CLK_CNTL);
    zx_nanosleep(zx_deadline_after(ZX_USEC(500)));

    temp = readl(preset_regs + RESET0_LEVEL_REGISTER);
    temp |= 1 << 20;
    writel(temp, preset_regs + RESET0_LEVEL_REGISTER);

    temp = readl(preset_regs + RESET2_LEVEL_REGISTER);
    temp |= 1 << 14;
    writel(temp, preset_regs + RESET2_LEVEL_REGISTER);

#define PWR_KEY 0x50
#define PWR_OVERRIDE1 0x58

    writel(0x2968A819, gpu_regs + PWR_KEY);
    writel(0xfff | (0x20 << 16), gpu_regs + PWR_OVERRIDE1);

    if ((status = pbus_device_add(&bus->pbus, &mali_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "vim_start_thread could not add mali_dev: %d\n", status);
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
