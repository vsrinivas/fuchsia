// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
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

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "vim.h"
#include "vim-hw.h"

// display MMIO for VIM board
static const pbus_mmio_t vim_display_mmios[] = {
    {
        .base = 0x3d800000,
        .length = 1920 * 1080 * 2,
    },
};

// display MMIO for VIM2 board
static const pbus_mmio_t vim2_display_mmios[] = {
    {
        .base = 0xbd851000,
        .length = 1920 * 1080 * 2,
    },
};

static pbus_dev_t display_dev = {
    .name = "display",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM,
    .did = PDEV_DID_VIM_DISPLAY,
    .mmios = vim_display_mmios,
    .mmio_count = countof(vim_display_mmios),
};

static void vim_bus_release(void* ctx) {
    vim_bus_t* bus = ctx;

//    a113_gpio_release(&bus->gpio);
    free(bus);
}

static zx_protocol_device_t vim_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = vim_bus_release,
};

static zx_status_t vim_bus_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    vim_bus_t* bus = calloc(1, sizeof(vim_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus)) != ZX_OK) {
        goto fail;
    }

/*
    if ((status = a113_gpio_init(&bus->gpio)) != ZX_OK) {
        zxlogf(ERROR, "a113_gpio_init failed: %d\n", status);
        goto fail;
    }

    if ((status = a113_i2c_init(&bus->i2c)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init failed: %d\n", status);
        goto fail;
    }
*/

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "vim-bus",
        .ctx = bus,
        .ops = &vim_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    if ((status = vim_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_usb_init failed: %d\n", status);
    }

    // VIM2 board has different framebuffer address
    if (!strcmp(pbus_get_board_name(&bus->pbus), "vim2")) {
        display_dev.mmios = vim2_display_mmios;
    }

    if ((status = pbus_device_add(&bus->pbus, &display_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "vim_usb_init could not add display_dev: %d\n", status);
        return status;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "vim_bus_bind failed %d\n", status);
    vim_bus_release(bus);
    return status;
}

static zx_driver_ops_t vim_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vim_bus_bind,
};

ZIRCON_DRIVER_BEGIN(vim_bus, vim_bus_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
ZIRCON_DRIVER_END(vim_bus)
