// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/mt8167/mt8167-hw.h>
#include "mt8167.h"

namespace board_mt8167 {

namespace {
constexpr pbus_mmio_t dsi_mmios[] = {
    // DSI0
    {
        .base = MT8167_DISP_DSI_BASE,
        .length = MT8167_DISP_DSI_SIZE,
    },
};

constexpr pbus_mmio_t display_mmios[] = {
    // Overlay
    {
        .base = MT8167_DISP_OVL_BASE,
        .length = MT8167_DISP_OVL_SIZE,
    },
    // Display RDMA
    {
        .base = MT8167_DISP_RDMA_BASE,
        .length = MT8167_DISP_RDMA_SIZE,
    },
    // MIPI_TX
    {
        .base = MT8167_MIPI_TX_BASE,
        .length = MT8167_MIPI_TX_SIZE,
    },
};

constexpr pbus_gpio_t display_gpios[] = {
    {
        .gpio = MT8167_GPIO_LCD_RST
    },
};

constexpr display_driver_t display_driver_info[] = {
    {
        .vid = PDEV_VID_MEDIATEK,
        .pid = PDEV_PID_MEDIATEK_8167S_REF,
        .did = PDEV_DID_MEDIATEK_DISPLAY,
    },
};

constexpr pbus_metadata_t display_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &display_driver_info,
        .data_size = sizeof(display_driver_t),
    },
};

constexpr pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};
constexpr pbus_irq_t display_irqs[] = {
    {
        .irq  = MT8167_IRQ_DISP_OVL0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};
constexpr uint32_t display_protocols[] = {
    ZX_PROTOCOL_SYSMEM,
};

static pbus_dev_t display_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "display";
    dev.vid = PDEV_VID_MEDIATEK;
    dev.did = PDEV_DID_MEDIATEK_DISPLAY;
    dev.mmio_list = display_mmios;
    dev.mmio_count = countof(display_mmios);
    dev.bti_list = display_btis;
    dev.bti_count = countof(display_btis);
    dev.irq_list = display_irqs;
    dev.irq_count = countof(display_irqs);
    dev.gpio_list = display_gpios;
    dev.gpio_count = countof(display_gpios);
    return dev;
}();

static pbus_dev_t dsi_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "dw-dsi";
    dev.vid = PDEV_VID_MEDIATEK;
    dev.did = PDEV_DID_MEDIATEK_DSI;
    dev.metadata_list = display_metadata;
    dev.metadata_count = countof(display_metadata);
    dev.mmio_list = dsi_mmios;
    dev.mmio_count =countof(dsi_mmios);
    dev.child_list = &display_dev;
    dev.child_count = 1;
    dev.protocol_list = display_protocols;
    dev.protocol_count = countof(display_protocols);
    return dev;
}();
} // namespace

zx_status_t Mt8167::DisplayInit() {
    zx_status_t status = pbus_.DeviceAdd(&dsi_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace board_mt8167
