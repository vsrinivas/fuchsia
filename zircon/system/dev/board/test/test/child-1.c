// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>

#define DRIVER_NAME "test-child-1"

typedef struct {
    zx_device_t* zxdev;
} test_t;

static void test_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = test_release,
};

static zx_status_t test_gpio(pdev_protocol_t* pdev) {
    zx_status_t status;
    gpio_protocol_t gpio;
    size_t actual;

    status = pdev_get_protocol(pdev, ZX_PROTOCOL_GPIO, 0, &gpio, sizeof(gpio), &actual);

    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to get gpio, st = %d\n", DRIVER_NAME, status);
    }

    return status;
}

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
    pdev_protocol_t pdev;
    zx_status_t status;

    zxlogf(INFO, "test_bind: %s \n", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV\n", DRIVER_NAME);
        return status;
    }

    status = test_gpio(&pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: gpio test failed, st = %d\n", DRIVER_NAME, status);
    }

    test_t* child_2 = calloc(1, sizeof(test_t));
    if (!child_2) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_device_prop_t child_2_props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_PDEV },
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_PBUS_TEST},
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_CHILD_2 },
    };

    device_add_args_t child_2_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "child-2-top",
        .ctx = child_2,
        .ops = &test_device_protocol,
        .props = child_2_props,
        .prop_count = countof(child_2_props),
    };

    status = pdev_device_add(&pdev, 0, &child_2_args, &child_2->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_device_add failed: %d\n", DRIVER_NAME, status);
        free(child_2);
        return status;
    }

    test_t* child_3 = calloc(1, sizeof(test_t));
    if (!child_3) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_device_prop_t child_3_props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_PDEV },
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_PBUS_TEST },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_CHILD_3 },
    };

    device_add_args_t child_3_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "child-3-top",
        .ctx = child_3,
        .ops = &test_device_protocol,
        .props = child_3_props,
        .prop_count = countof(child_3_props),
    };

    status = pdev_device_add(&pdev, 1, &child_3_args, &child_3->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_device_add failed: %d\n", DRIVER_NAME, status);
        free(child_3);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

ZIRCON_DRIVER_BEGIN(test_bus, test_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CHILD_1),
ZIRCON_DRIVER_END(test_bus)
