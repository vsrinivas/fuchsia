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

#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-s912/s912-gpio.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "aml.h"

static void aml_bus_release(void* ctx) {
    aml_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t aml_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_bus_release,
};

static int aml_start_thread(void* arg) {
    aml_bus_t* bus = arg;
    zx_status_t status;

    if ((status = aml_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init failed: %d\n", status);
        goto fail;
    }
/*
    if ((status = aml_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init failed: %d\n", status);
        goto fail;
    }
    if ((status = aml_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init failed: %d\n", status);
        goto fail;
    }
*/

    return ZX_OK;
fail:
    zxlogf(ERROR, "aml_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t aml_bus_bind(void* ctx, zx_device_t* parent) {
    aml_bus_t* bus = calloc(1, sizeof(aml_bus_t));
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
        zxlogf(ERROR, "aml_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-bus",
        .ctx = bus,
        .ops = &aml_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, aml_start_thread, bus, "aml_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }
    return ZX_OK;

fail:
    zxlogf(ERROR, "aml_bus_bind failed %d\n", status);
    aml_bus_release(bus);
    return status;
}

static zx_driver_ops_t aml_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_bus_bind,
};

ZIRCON_DRIVER_BEGIN(aml_bus, aml_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
ZIRCON_DRIVER_END(vim_bus)
