// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>

#include "astro.h"
#include "astro-gpios.h"

static const pbus_mmio_t display_mmios[] = {
    {
        // VBUS/VPU
        .base = S905D2_VPU_BASE,
        .length = S905D2_VPU_LENGTH,
    },
    {
        // TOP DSI Host Controller (Amlogic Specific)
        .base = S905D2_MIPI_TOP_DSI_BASE,
        .length = S905D2_MIPI_TOP_DSI_LENGTH,
    },
    {
        // DSI PHY
        .base = S905D2_DSI_PHY_BASE,
        .length = S905D2_DSI_PHY_LENGTH,
    },
    {
        // HHI
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        // AOBUS
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    },
    {
        // CBUS
        .base = S905D2_CBUS_BASE,
        .length = S905D2_CBUS_LENGTH,
    },
};

static const pbus_irq_t display_irqs[] = {
    {
        .irq = S905D2_VIU1_VSYNC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_RDMA_DONE,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const display_driver_t display_driver_info[] = {
    {
        .vid = PDEV_VID_AMLOGIC,
        .pid = PDEV_PID_AMLOGIC_S905D2,
        .did = PDEV_DID_AMLOGIC_DISPLAY,
    },
};

static const pbus_metadata_t display_metadata[] = {
    {
        .type = DEVICE_METADATA_DISPLAY_DEVICE,
        .data_buffer = &display_driver_info,
        .data_size = sizeof(display_driver_t),
    },
};

static const pbus_bti_t display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
    },
};

static const pbus_mmio_t dsi_mmios[] = {
    {
        // DSI Host Controller
        .base = S905D2_MIPI_DSI_BASE,
        .length = S905D2_MIPI_DSI_LENGTH,
    },
};

static const pbus_dev_t display_dev = {
    .name = "display",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_DISPLAY,
    .mmio_list = display_mmios,
    .mmio_count = countof(display_mmios),
    .irq_list = display_irqs,
    .irq_count = countof(display_irqs),
    .bti_list = display_btis,
    .bti_count = countof(display_btis),
};

static const pbus_dev_t dsi_dev = {
    .name = "dw-dsi",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DW_DSI,
    .metadata_list = display_metadata,
    .metadata_count = countof(display_metadata),
    .mmio_list = dsi_mmios,
    .mmio_count =countof(dsi_mmios),
};

// Composite binding rules for display driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t dsi_match[]  = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_DSI_IMPL),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_DISPLAY),
};
static const zx_bind_inst_t panel_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_PANEL_DETECT),
};
static const zx_bind_inst_t lcd_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LCD_RESET),
};
static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static const zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
static const device_component_part_t dsi_component[] = {
    { countof(root_match), root_match },
    { countof(dsi_match), dsi_match },
};
static const device_component_part_t panel_gpio_component[] = {
    { countof(root_match), root_match },
    { countof(panel_gpio_match), panel_gpio_match },
};
static const device_component_part_t lcd_gpio_component[] = {
    { countof(root_match), root_match },
    { countof(lcd_gpio_match), lcd_gpio_match },
};
static const device_component_part_t sysmem_component[] = {
    { countof(root_match), root_match },
    { countof(sysmem_match), sysmem_match },
};
static const device_component_part_t canvas_component[] = {
    { countof(root_match), root_match },
    { countof(canvas_match), canvas_match },
};
static const device_component_t components[] = {
    { countof(dsi_component), dsi_component },
    { countof(panel_gpio_component), panel_gpio_component },
    { countof(lcd_gpio_component), lcd_gpio_component },
    { countof(sysmem_component), sysmem_component },
    { countof(canvas_component), canvas_component },
};

zx_status_t aml_display_init(aml_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &dsi_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add dsi dev: %d\n", __FUNCTION__, status);
        return status;
    }

    status = pbus_composite_device_add(&bus->pbus, &display_dev, components, countof(components),
                                       1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_composite_device_add failed: %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}
