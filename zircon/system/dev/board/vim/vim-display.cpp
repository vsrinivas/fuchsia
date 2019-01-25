// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim.h"
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

namespace vim {
// DMC MMIO for display driver
static pbus_mmio_t vim_display_mmios[] = {
    {
        .base = S912_PRESET_BASE,
        .length = S912_PRESET_LENGTH,
    },
    {
        .base = S912_HDMITX_BASE,
        .length = S912_HDMITX_LENGTH,
    },
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
    {
        .base = S912_VPU_BASE,
        .length = S912_VPU_LENGTH,
    },
    {
        .base = S912_HDMITX_SEC_BASE,
        .length = S912_HDMITX_SEC_LENGTH,
    },
    {
        .base = S912_DMC_REG_BASE,
        .length = S912_DMC_REG_LENGTH,
    },
    {
        .base = S912_CBUS_REG_BASE,
        .length = S912_CBUS_REG_LENGTH,
    },
    {
        .base = S912_AUDOUT_BASE,
        .length = S912_AUDOUT_LEN,
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
    {
        .irq = S912_RDMA_DONE_IRQ,
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

static const uint32_t vim_display_protocols[] = {
    ZX_PROTOCOL_SYSMEM,
    ZX_PROTOCOL_AMLOGIC_CANVAS,
};

zx_status_t Vim::DisplayInit() {
    zx_status_t status;
    pbus_dev_t display_dev = {};
    display_dev.name = "display";
    display_dev.vid = PDEV_VID_KHADAS;
    display_dev.pid = PDEV_PID_VIM2;
    display_dev.did = PDEV_DID_VIM_DISPLAY;
    display_dev.mmio_list = vim_display_mmios;
    display_dev.mmio_count = countof(vim_display_mmios);
    display_dev.irq_list = vim_display_irqs;
    display_dev.irq_count = countof(vim_display_irqs);
    display_dev.gpio_list = vim_display_gpios;
    display_dev.gpio_count = countof(vim_display_gpios);
    display_dev.bti_list = vim_display_btis;
    display_dev.bti_count = countof(vim_display_btis);
    display_dev.protocol_list = vim_display_protocols;
    display_dev.protocol_count = countof(vim_display_protocols);

    // enable this #if 0 in order to enable the SPDIF out pin for VIM2 (GPIO H4, pad M22)
#if 0
    gpio_set_alt_function(&bus->gpio, S912_SPDIF_H4, S912_SPDIF_H4_OUT_FN);
#endif

    if ((status = pbus_.DeviceAdd(&display_dev)) != ZX_OK) {
        zxlogf(ERROR, "DisplayInit: pbus_device_add() failed for display: %d\n", status);
        return status;
    }

    return ZX_OK;
}
} //namespace vim
