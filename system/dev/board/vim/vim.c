// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-s912/s912-gpio.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "vim.h"
#include "vim2-hw.h"

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
};

const pbus_gpio_t vim_display_gpios[] = {
    {
        // HPD
        .gpio = S912_GPIOH(0),
    },
};

static const pbus_bti_t vim_display_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_DISPLAY,
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
    .btis = vim_display_btis,
    .bti_count = countof(vim_display_btis),
};

static void vim_bus_release(void* ctx) {
    vim_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t vim_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = vim_bus_release,
};

static const pbus_i2c_channel_t led2472g_channels[] = {
  {
    .bus_id = 0,
    .address = 0x46,
  },
};

static const pbus_dev_t led2472g_dev = {
  .name = "led2472g",
  .vid = PDEV_VID_GENERIC,
  .pid = PDEV_PID_GENERIC,
  .did = PDEV_DID_LED2472G,
  .i2c_channels = led2472g_channels,
  .i2c_channel_count = countof(led2472g_channels),
};

static int vim_start_thread(void* arg) {
    vim_bus_t* bus = arg;
    zx_status_t status;

    if ((status = vim_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_i2c_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_uart_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_uart_init failed: %d\n", status);
        goto fail;
    }
    if ((status = vim_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "vim_usb_init failed: %d\n", status);
        goto fail;
    }

    if (bus->soc_pid == PDEV_PID_AMLOGIC_S912) {
        if ((status = vim_mali_init(bus)) != ZX_OK) {
            zxlogf(ERROR, "vim_mali_init failed: %d\n", status);
            goto fail;
        }
    }

    if (bus->soc_pid == PDEV_PID_AMLOGIC_S912) {
        // set VIM2 fan to level 3 (fastest speed)
        // TODO(voydanoff) replace this with a thermal driver
        gpio_config(&bus->gpio, VIM2_FAN_CTL0, GPIO_DIR_OUT);
        gpio_config(&bus->gpio, VIM2_FAN_CTL1, GPIO_DIR_OUT);
        gpio_write(&bus->gpio, VIM2_FAN_CTL0, 1);
        gpio_write(&bus->gpio, VIM2_FAN_CTL1, 1);
    }

    // Display driver currently supports only the S912
    if (bus->soc_pid == PDEV_PID_AMLOGIC_S912) {
        if ((status = pbus_device_add(&bus->pbus, &display_dev, 0)) != ZX_OK) {
            zxlogf(ERROR, "vim_start_thread could not add display_dev: %d\n", status);
            goto fail;
        }
    }

    if ((status = pbus_device_add(&bus->pbus, &led2472g_dev, 0)) != ZX_OK) {
      zxlogf(ERROR, "vim_start_thread could not add led2472g_dev: %d\n", status);
      goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "vim_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t vim_bus_bind(void* ctx, zx_device_t* parent) {
    vim_bus_t* bus = calloc(1, sizeof(vim_bus_t));
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
        zxlogf(ERROR, "vim_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }

    const char* board_name = pbus_get_board_name(&bus->pbus);
    if (!strcmp(board_name, "vim")) {
        bus->soc_pid = PDEV_PID_AMLOGIC_S905X;
    } else if (!strcmp(board_name, "vim2")) {
        bus->soc_pid = PDEV_PID_AMLOGIC_S912;
    } else {
        zxlogf(ERROR, "unsupported board %s\n", board_name);
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

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

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, vim_start_thread, bus, "vim_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
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
