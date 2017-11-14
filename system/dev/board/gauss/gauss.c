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

#include "gauss.h"
#include "gauss-hw.h"

// turn this on to enable Gauss accelerometer test driver
//#define I2C_TEST 1

#if I2C_TEST
static const pbus_i2c_channel_t i2c_channels[] = {
    {
        // Gauss accelerometer
        .bus_id = AML_I2C_B,
        .address = 0x18,
    },
};

static const pbus_dev_t i2c_test_dev = {
    .name = "i2c-test",
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_GAUSS,
    .did = PDEV_DID_GAUSS_I2C_TEST,
    .i2c_channels = i2c_channels,
    .i2c_channel_count = countof(i2c_channels),
};
#endif

static zx_status_t gauss_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    *out_mode = USB_MODE_HOST;
    return ZX_OK;
}

static zx_status_t gauss_set_mode(void* ctx, usb_mode_t mode) {
    gauss_bus_t* bus = ctx;
    return gauss_usb_set_mode(bus, mode);
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .get_initial_mode = gauss_get_initial_mode,
    .set_mode = gauss_set_mode,
};

static zx_status_t gauss_bus_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    gauss_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        memcpy(out, &bus->usb_mode_switch, sizeof(bus->usb_mode_switch));
        return ZX_OK;
    case ZX_PROTOCOL_GPIO:
        memcpy(out, &bus->a113->gpio, sizeof(bus->a113->gpio));
        return ZX_OK;
    case ZX_PROTOCOL_I2C:
        memcpy(out, &bus->a113->i2c, sizeof(bus->a113->i2c));
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static pbus_interface_ops_t gauss_bus_ops = {
    .get_protocol = gauss_bus_get_protocol,
};

static void gauss_bus_release(void* ctx) {
    gauss_bus_t* bus = ctx;

    if (bus->a113) {
        a113_bus_release(bus->a113);
    }
    free(bus);
}

static zx_protocol_device_t gauss_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = gauss_bus_release,
};

static zx_status_t gauss_bus_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    gauss_bus_t* bus = calloc(1, sizeof(gauss_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus)) != ZX_OK) {
        goto fail;
    }
    if ((status = a113_bus_init(&bus->a113)) != ZX_OK) {
        goto fail;
    }

    // pinmux for Gauss i2c
    a113_pinmux_config(bus->a113, I2C_SCK_A, 1);
    a113_pinmux_config(bus->a113, I2C_SDA_A, 1);
    a113_pinmux_config(bus->a113, I2C_SCK_B, 1);
    a113_pinmux_config(bus->a113, I2C_SDA_B, 1);

    // Config pinmux for gauss PDM pins
    a113_pinmux_config(bus->a113, A113_GPIOA(14), 1);
    a113_pinmux_config(bus->a113, A113_GPIOA(15), 1);
    a113_pinmux_config(bus->a113, A113_GPIOA(16), 1);
    a113_pinmux_config(bus->a113, A113_GPIOA(17), 1);
    a113_pinmux_config(bus->a113, A113_GPIOA(18), 1);

    bus->usb_mode_switch.ops = &usb_mode_switch_ops;
    bus->usb_mode_switch.ctx = bus;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "gauss-bus",
        .ctx = bus,
        .ops = &gauss_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    pbus_interface_t intf;
    intf.ops = &gauss_bus_ops;
    intf.ctx = bus;
    pbus_set_interface(&bus->pbus, &intf);

    if ((status = gauss_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_usb_init failed: %d\n", status);
    }
    if ((status = gauss_audio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "gauss_audio_init failed: %d\n", status);
    }

#if I2C_TEST
    if ((status = pbus_device_add(&bus->pbus, &i2c_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init could not add i2c_test_dev: %d\n", status);
    }
#endif

    return ZX_OK;

fail:
    printf("gauss_bus_bind failed %d\n", status);
    gauss_bus_release(bus);
    return status;
}

static zx_driver_ops_t gauss_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gauss_bus_bind,
};

ZIRCON_DRIVER_BEGIN(gauss_bus, gauss_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_GAUSS),
ZIRCON_DRIVER_END(gauss_bus)
