// Copyright 2016 The Fuchsia Authors. All rights reserved.
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

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

#include "hikey960.h"
#include "hikey960-hw.h"

static zx_status_t hikey960_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    *out_mode = USB_MODE_HOST;
    return ZX_OK;
}

static zx_status_t hikey960_set_mode(void* ctx, usb_mode_t mode) {
    hikey960_t* hikey = ctx;

    if (mode == hikey->usb_mode) {
        return ZX_OK;
    }
    if (mode == USB_MODE_OTG) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    gpio_protocol_t gpio;
    zx_status_t status = hi3660_get_protocol(hikey->hi3660, ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
        return status;
    }

    gpio_config(&gpio, GPIO_HUB_VDD33_EN, GPIO_DIR_OUT);
    gpio_config(&gpio, GPIO_VBUS_TYPEC, GPIO_DIR_OUT);
    gpio_config(&gpio, GPIO_USBSW_SW_SEL, GPIO_DIR_OUT);

    gpio_write(&gpio, GPIO_HUB_VDD33_EN, mode == USB_MODE_HOST);
    gpio_write(&gpio, GPIO_VBUS_TYPEC, mode == USB_MODE_HOST);
    gpio_write(&gpio, GPIO_USBSW_SW_SEL, mode == USB_MODE_HOST);

    // add or remove XHCI device
    pbus_device_enable(&hikey->pbus, PDEV_VID_GENERIC, PDEV_PID_GENERIC, PDEV_DID_USB_XHCI,
                       mode == USB_MODE_HOST);

    hikey->usb_mode = mode;
    return ZX_OK;
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .get_initial_mode = hikey960_get_initial_mode,
    .set_mode = hikey960_set_mode,
};

static void hikey960_release(void* ctx) {
    hikey960_t* hikey = ctx;

    if (hikey->hi3660) {
        hi3660_release(hikey->hi3660);
    }
    free(hikey);
}

static zx_protocol_device_t hikey960_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = hikey960_release,
};

static zx_status_t hikey960_bind(void* ctx, zx_device_t* parent) {
    hikey960_t* hikey = calloc(1, sizeof(hikey960_t));
    if (!hikey) {
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &hikey->pbus) != ZX_OK) {
        free(hikey);
        return ZX_ERR_NOT_SUPPORTED;
    }
    hikey->usb_mode = USB_MODE_NONE;

    // TODO(voydanoff) get from platform bus driver somehow
    zx_handle_t resource = get_root_resource();
    zx_status_t status = hi3660_init(resource, &hikey->hi3660);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hikey960_bind: hi3660_init failed %d\n", status);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "hikey960",
        .ctx = hikey,
        .ops = &hikey960_device_protocol,
        // nothing should bind to this device
        // all interaction will be done via the pbus_interface_t
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    hikey->usb_mode_switch.ops = &usb_mode_switch_ops;
    hikey->usb_mode_switch.ctx = hikey;

    status = pbus_set_protocol(&hikey->pbus, ZX_PROTOCOL_USB_MODE_SWITCH, &hikey->usb_mode_switch);
    if (status != ZX_OK) {
        goto fail;
    }

    gpio_protocol_t gpio;
    status = hi3660_get_protocol(hikey->hi3660, ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
        goto fail;
    }
    status = pbus_set_protocol(&hikey->pbus, ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
        goto fail;
    }

    i2c_protocol_t i2c;
    status = hi3660_get_protocol(hikey->hi3660, ZX_PROTOCOL_I2C, &i2c);
    if (status != ZX_OK) {
        goto fail;
    }
    status = pbus_set_protocol(&hikey->pbus, ZX_PROTOCOL_I2C, &i2c);
    if (status != ZX_OK) {
        goto fail;
    }

    if ((status = hikey960_add_devices(hikey)) != ZX_OK) {
        zxlogf(ERROR, "hikey960_bind: hi3660_add_devices failed!\n");;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "hikey960_bind failed %d\n", status);
    hikey960_release(hikey);
    return status;
}

static zx_driver_ops_t hikey960_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hikey960_bind,
};

ZIRCON_DRIVER_BEGIN(hikey960, hikey960_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960),
ZIRCON_DRIVER_END(hikey960)
