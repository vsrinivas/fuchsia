// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/aml-t931/t931-gpio.h>
#include "sherlock.h"

namespace sherlock {

namespace {
constexpr pbus_mmio_t display_mmios[] = {
    {
        // VBUS/VPU
        .base = T931_VPU_BASE,
        .length = T931_VPU_LENGTH,
    },
    {
        // DSI Host Controller
        .base = T931_MIPI_DSI_BASE,
        .length = T931_MIPI_DSI_LENGTH,
    },
    {
        // DSI PHY
        .base = T931_DSI_PHY_BASE,
        .length = T931_DSI_PHY_LENGTH,
    },
    {
        // HHI
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        // CBUS
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
};

static const pbus_irq_t display_irqs[] = {
    {
        .irq = T931_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_RDMA_DONE,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_gpio_t display_gpios[] = {
    {
        // Backlight Enable
        .gpio = T931_GPIOA(10),
    },
    {
        // LCD Reset
        .gpio = T931_GPIOH(6),
    },
    {
        // Panel detection
        .gpio = T931_GPIOH(0),
    },
};

static const pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};

static const pbus_i2c_channel_t display_i2c_channels[] = {
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x2C,
    },
};

static const uint32_t display_protocols[] = {
    ZX_PROTOCOL_SYSMEM,
    ZX_PROTOCOL_AMLOGIC_CANVAS,
};

static pbus_dev_t display_dev = []() {
    pbus_dev_t dev;
    dev.name = "display";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_S905D2;
    dev.did = PDEV_DID_AMLOGIC_DISPLAY;
    dev.mmio_list = display_mmios;
    dev.mmio_count = countof(display_mmios);
    dev.irq_list = display_irqs;
    dev.irq_count = countof(display_irqs);
    dev.gpio_list = display_gpios;
    dev.gpio_count = countof(display_gpios);
    dev.bti_list = display_btis;
    dev.bti_count = countof(display_btis);
    dev.i2c_channel_list = display_i2c_channels;
    dev.i2c_channel_count = countof(display_i2c_channels);
    dev.protocol_list = display_protocols;
    dev.protocol_count = countof(display_protocols);
    return dev;
}();

} // namespace

zx_status_t Sherlock::DisplayInit() {

    zx_status_t status = pbus_.DeviceAdd(&display_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return status;
}

} // namespace sherlock
