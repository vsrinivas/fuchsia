// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-s912/s912-gpio.h>
#include "vim.h"

// DMC MMIO for display driver
static pbus_mmio_t vim_display_mmios[] = {
    {
        .base =     S912_PRESET_BASE,
        .length =   S912_PRESET_LENGTH,
    },
    {
        .base =     S912_HDMITX_BASE,
        .length =   S912_HDMITX_LENGTH,
    },
    {
        .base =     S912_HIU_BASE,
        .length =   S912_HIU_LENGTH,
    },
    {
        .base =     S912_VPU_BASE,
        .length =   S912_VPU_LENGTH,
    },
    {
        .base =     S912_HDMITX_SEC_BASE,
        .length =   S912_HDMITX_SEC_LENGTH,
    },
    {
        .base =     S912_DMC_REG_BASE,
        .length =   S912_DMC_REG_LENGTH,
    },
    {
        .base =     S912_CBUS_REG_BASE,
        .length =   S912_CBUS_REG_LENGTH,
    },
    {
        .base =     S912_AUDOUT_BASE,
        .length =   S912_AUDOUT_LEN,
    },
};

const pbus_gpio_t vim_display_gpios[] = {
    {
        // HPD
        .gpio = S912_GPIOH(0),
    },
};

static const pbus_irq_t vim_display_irqs[] = {
    {
        .irq = S912_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t vim_display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
    {
        .iommu_index = 0,
        .bti_id = BTI_AUDIO,
    },
};

static const pbus_dev_t display_dev = {
    .name = "display",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_VIM_DISPLAY,
    .mmios = vim_display_mmios,
    .mmio_count = countof(vim_display_mmios),
    .gpios = vim_display_gpios,
    .gpio_count = countof(vim_display_gpios),
    .irqs = vim_display_irqs,
    .irq_count = countof(vim_display_irqs),
    .btis = vim_display_btis,
    .bti_count = countof(vim_display_btis),
};

zx_status_t vim_display_init(vim_bus_t* bus) {
    zx_status_t status;

    // enable this #if 0 in order to enable the SPDIF out pin for VIM2 (GPIO H4, pad M22)
#if 0
    gpio_set_alt_function(&bus->gpio, S912_SPDIF_H4, S912_SPDIF_H4_OUT_FN);
#endif

    if ((status = pbus_device_add(&bus->pbus, &display_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "vim_display_init: pbus_device_add() failed for display: %d\n", status);
        return status;
    }

    return ZX_OK;
}
