// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <soc/imx8m/imx8m.h>
#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "imx8mevk.h"

static zx_status_t imx8mevk_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    imx8mevk_bus_t* bus = ctx;
    *out_mode = bus->initial_usb_mode;
    return ZX_OK;
}

static zx_status_t imx8mevk_set_mode(void* ctx, usb_mode_t mode) {
    imx8mevk_bus_t* bus = ctx;

    if (mode == bus->usb_mode) {
        return ZX_OK;
    }
    if (mode == USB_MODE_OTG) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // add or remove XHCI device
    pbus_device_enable(&bus->pbus, PDEV_VID_GENERIC, PDEV_PID_GENERIC, PDEV_DID_USB_XHCI,
                       mode == USB_MODE_HOST);

    bus->usb_mode = mode;
    return ZX_OK;
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .get_initial_mode = imx8mevk_get_initial_mode,
    .set_mode = imx8mevk_set_mode,
};


/* iMX8M EVK Pin Mux Table TODO: Add all supported peripherals on EVK board */
iomux_cfg_struct imx8mevk_pinmux[] = {
    // UART1 RX
    MAKE_PIN_CFG_UART(0, SW_MUX_CTL_PAD_UART1_RXD,
                            SW_PAD_CTL_PAD_UART1_RXD,
                            UART1_RXD_SELECT_INPUT),
    // UART1 TX
    MAKE_PIN_CFG_UART(0, SW_MUX_CTL_PAD_UART1_TXD,
                            SW_PAD_CTL_PAD_UART1_TXD,
                            0x000ULL),

    // PWR_LED (used for GPIO Driver)
    MAKE_PIN_CFG_DEFAULT(0,  SW_MUX_CTL_PAD_GPIO1_IO13),
};

static void imx8mevk_bus_release(void* ctx) {
    imx8mevk_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t imx8mevk_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = imx8mevk_bus_release,
};

static int imx8mevk_start_thread(void* arg) {
    zx_status_t status;
    imx8mevk_bus_t* bus = arg;

    bus->usb_mode_switch.ops = &usb_mode_switch_ops;
    bus->usb_mode_switch.ctx = bus;
    bus->initial_usb_mode = USB_MODE_HOST;
    // TODO: Power and Clocks

    // start the gpio driver first so we can do our initial pinmux
    if ((status = imx8m_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "%s: failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    // Pinmux
    for (unsigned i = 0; i < sizeof(imx8mevk_pinmux)/sizeof(imx8mevk_pinmux[0]); i++) {
        gpio_set_alt_function(&bus->gpio, 0, imx8mevk_pinmux[i]);
    }

    if ((status = imx_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "%s: failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    status = pbus_set_protocol(&bus->pbus, ZX_PROTOCOL_USB_MODE_SWITCH, &bus->usb_mode_switch);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "aml_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t imx8mevk_bus_bind(void* ctx, zx_device_t* parent)
{

    imx8mevk_bus_t* bus = calloc(1, sizeof(imx8mevk_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }
    bus->parent = parent;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus);
    if (status != ZX_OK) {
        goto fail;
    }

    // get default BTI from the dummy IOMMU implementation in the platform bus
    status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &bus->iommu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not get ZX_PROTOCOL_IOMMU\n", __FUNCTION__);
        goto fail;
    }

    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bus->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: iommu_get_bti failed %d\n", __FUNCTION__, status);
        goto fail;
    }

    const char* board_name = pbus_get_board_name(&bus->pbus);
    if (!strcmp(board_name, "imx8mevk")) {
        bus->soc_pid = PDEV_VID_NXP;
    } else {
        zxlogf(ERROR, "%s: Invalid/Unsupported board (%s)\n", __FUNCTION__, board_name);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx8mevk",
        .ctx = bus,
        .ops = &imx8mevk_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, imx8mevk_start_thread, bus, "imx8mevk_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "%s failed. %d\n", __FUNCTION__, status);
    imx8mevk_bus_release(bus);
    return status;
}

static zx_driver_ops_t imx8mevk_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx8mevk_bus_bind,
};

ZIRCON_DRIVER_BEGIN(vim_bus, imx8mevk_bus_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MEVK),
ZIRCON_DRIVER_END(vim_bus)
