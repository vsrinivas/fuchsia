// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "aml-fanctl.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/scpi.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

void aml_set_fan_level(aml_fanctl_t *fanctl, uint32_t level) {
    switch(level) {
        case 0:
            gpio_write(&fanctl->gpio, FAN_CTL0, 0);
            gpio_write(&fanctl->gpio, FAN_CTL1, 0);
            break;
        case 1:
            gpio_write(&fanctl->gpio, FAN_CTL0, 1);
            gpio_write(&fanctl->gpio, FAN_CTL1, 0);
            break;
        case 2:
            gpio_write(&fanctl->gpio, FAN_CTL0, 0);
            gpio_write(&fanctl->gpio, FAN_CTL1, 1);
            break;
        case 3:
            gpio_write(&fanctl->gpio, FAN_CTL0, 1);
            gpio_write(&fanctl->gpio, FAN_CTL1, 1);
            break;
        default:
            break;
    }
}

static int aml_fanctl_init_thread(void *ctx) {
    aml_fanctl_t *fanctl = ctx;
    uint32_t temp_sensor_id;
    uint32_t temperature;

    // Get the sensor ID
    zx_status_t status = scpi_get_sensor(&fanctl->scpi, "aml_thermal", &temp_sensor_id);
    if (status != ZX_OK) {
        FANCTL_ERROR("Unable to get thermal sensor information: Thermal disabled\n");
        return ZX_OK;
    }

    while(true) {
        status = scpi_get_sensor_value(&fanctl->scpi, temp_sensor_id, &temperature);
        if (status != ZX_OK) {
            FANCTL_ERROR("Unable to get thermal sensor value: Thermal disabled\n");
            return ZX_OK;
        }

        if (temperature < TRIGGER_LEVEL_0) {
            aml_set_fan_level(fanctl, 0);
        } else if (temperature < TRIGGER_LEVEL_1) {
            aml_set_fan_level(fanctl, 1);
        } else if (temperature < TRIGGER_LEVEL_2) {
            aml_set_fan_level(fanctl, 2);
        } else {
            aml_set_fan_level(fanctl, 3);
        }

        sleep(5);
    }
    return ZX_OK;
}

static void aml_fanctl_release(void* ctx) {
    aml_fanctl_t *fanctl = ctx;
    thrd_join(fanctl->main_thread, NULL);
    free(fanctl);
}

static zx_protocol_device_t aml_fanctl_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_fanctl_release,
};

static zx_status_t aml_fanctl_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_fanctl_t *fanctl = calloc(1, sizeof(aml_fanctl_t));
    if (!fanctl) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &fanctl->pdev);
    if (status !=  ZX_OK) {
        FANCTL_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &fanctl->gpio);
    if (status != ZX_OK) {
        FANCTL_ERROR("Could not get Fan-ctl GPIO protocol\n");
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_SCPI, &fanctl->scpi);
    if (status != ZX_OK) {
        FANCTL_ERROR("Could not get SCPI protocol\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&fanctl->pdev, &info);
    if (status != ZX_OK) {
        FANCTL_ERROR("pdev_get_device_info failed\n");
        goto fail;
    }

    // Configure the GPIOs
    for (uint32_t i=0; i<info.gpio_count; i++) {
        status = gpio_config(&fanctl->gpio, i, GPIO_DIR_OUT);
        if (status != ZX_OK) {
            FANCTL_ERROR("gpio_config failed\n");
            goto fail;
        }
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-fanctl",
        .ctx = fanctl,
        .ops = &aml_fanctl_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_create_with_name(&fanctl->main_thread, aml_fanctl_init_thread,
                          fanctl, "aml_fanctl_init_thread");

    return ZX_OK;
fail:
    aml_fanctl_release(fanctl);
    return ZX_OK;
}

static zx_driver_ops_t aml_fanctl_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = aml_fanctl_bind,
};

ZIRCON_DRIVER_BEGIN(aml_fanctl, aml_fanctl_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_FANCTL),
ZIRCON_DRIVER_END(aml_fanctl)