// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clock.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/platform/device.h>
#include <zircon/assert.h>

#define DRIVER_NAME "test-composite"

enum {
    COMPONENT_PDEV,
    COMPONENT_GPIO,
    COMPONENT_CLOCK,
    COMPONENT_I2C,
    COMPONENT_COUNT,
};

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

static zx_status_t test_gpio(gpio_protocol_t* gpio) {
    zx_status_t status;
    uint8_t value;

    if ((status = gpio_config_out(gpio, 0)) != ZX_OK) {
        return status;
    }
    if ((status = gpio_read(gpio, &value)) != ZX_OK || value != 0) {
        return status;
    }
    if ((status = gpio_write(gpio, 1)) != ZX_OK) {
        return status;
    }
    if ((status = gpio_read(gpio, &value)) != ZX_OK || value != 1) {
        return status;
    }

    return ZX_OK;
}

static zx_status_t test_clock(clock_protocol_t* clock) {
    zx_status_t status;

    // We should have 4 clocks, so the first 4 calls should succeed and the fifth fail.
    if ((status = clock_enable(clock, 0)) != ZX_OK) {
        return status;
    }
    if ((status = clock_disable(clock, 1)) != ZX_OK) {
        return status;
    }
    if ((status = clock_enable(clock, 2)) != ZX_OK) {
        return status;
    }
    if ((status = clock_disable(clock, 3)) != ZX_OK) {
        return status;
    }
    if ((status = clock_disable(clock, 4)) == ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

static zx_status_t test_i2c(i2c_protocol_t* i2c) {
    size_t max_transfer;

    // i2c test driver returns 1024 for max transfer size
    zx_status_t status = i2c_get_max_transfer_size(i2c, &max_transfer);
    if (status != ZX_OK || max_transfer != 1024) {
        zxlogf(ERROR, "%s: i2c_get_max_transfer_size failed\n", DRIVER_NAME);
        return ZX_ERR_INTERNAL;
    }

    // i2c test driver reverses digits
    const uint32_t write_digits[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    uint32_t read_digits[10];
    memset(read_digits, 0, sizeof(read_digits));

    status = i2c_write_read_sync(i2c, write_digits, sizeof(write_digits), read_digits,
                                 sizeof(read_digits));
    if (status != ZX_OK || max_transfer != 1024) {
        zxlogf(ERROR, "%s: i2c_write_read_sync failed %d\n", DRIVER_NAME, status);
        return status;
    }

    for (size_t i = 0; i < countof(read_digits); i++) {
        if (read_digits[i] != write_digits[countof(read_digits) - i - 1]) {
            zxlogf(ERROR, "%s: read_digits does not match reverse of write digits\n", DRIVER_NAME);
            return ZX_ERR_INTERNAL;
        }
    }

    return ZX_OK;
}

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
    composite_protocol_t composite;
    zx_status_t status;

    zxlogf(INFO, "test_bind: %s \n", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_COMPOSITE\n", DRIVER_NAME);
        return status;
    }

    uint32_t count = composite_get_component_count(&composite);
    size_t actual;
    zx_device_t* components[count];
    composite_get_components(&composite, components, count, &actual);
    if (count != actual || count != COMPONENT_COUNT) {
        zxlogf(ERROR, "%s: got the wrong number of components (%u, %zu)\n", DRIVER_NAME, count,
               actual);
        return ZX_ERR_BAD_STATE;
    }

    pdev_protocol_t pdev;
    gpio_protocol_t gpio;
    clock_protocol_t clock;
    i2c_protocol_t i2c;

    status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_PDEV\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_GPIO], ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_GPIO\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_CLOCK], ZX_PROTOCOL_CLOCK, &clock);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CLOCK\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_I2C], ZX_PROTOCOL_I2C, &i2c);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_I2C\n", DRIVER_NAME);
        return status;
    }

    if ((status = test_gpio(&gpio)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_gpio failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_clock(&clock)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_clock failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_i2c(&i2c)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_i2c failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    test_t* test = calloc(1, sizeof(test_t));
    if (!test) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "composite",
        .ctx = test,
        .ops = &test_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &test->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_add failed: %d\n", DRIVER_NAME, status);
        free(test);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

ZIRCON_DRIVER_BEGIN(test_bus, test_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_COMPOSITE),
ZIRCON_DRIVER_END(test_bus)
