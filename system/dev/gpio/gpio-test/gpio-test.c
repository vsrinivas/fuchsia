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
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

typedef struct {
    zx_device_t* zxdev;
    gpio_protocol_t* gpios;
    uint32_t gpio_count;
    thrd_t thread;
    thrd_t wait;
    bool done;
    zx_handle_t inth;
} gpio_test_t;

// GPIO indices (based on gpio_test_gpios)
enum {
    GPIO_LED,
    GPIO_BUTTON,
};

static void gpio_test_release(void* ctx) {
    gpio_test_t* gpio_test = ctx;
    gpio_protocol_t* gpios = gpio_test->gpios;

    gpio_test->done = true;
    zx_handle_close(gpio_test->inth);
    gpio_release_interrupt(&gpios[GPIO_BUTTON]);
    thrd_join(gpio_test->thread, NULL);
    thrd_join(gpio_test->wait, NULL);
    free(gpio_test->gpios);
    free(gpio_test);
}

static zx_protocol_device_t gpio_test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = gpio_test_release,
};

// test thread that cycles all of the GPIOs provided to us
static int gpio_test_thread(void *arg) {
    gpio_test_t* gpio_test = arg;
    gpio_protocol_t* gpios = gpio_test->gpios;
    uint32_t gpio_count = gpio_test->gpio_count;

    for (unsigned i = 0; i < gpio_count; i++) {
        if (gpio_config_out(&gpios[i], 0) != ZX_OK) {
            zxlogf(ERROR, "gpio-test: gpio_config failed for gpio %u\n", i);
            return -1;
        }
    }

    while (!gpio_test->done) {
        // Assuming here that the last GPIO is the input button
        // so we don't toggle that one
        for (unsigned i = 0; i < gpio_count-1; i++) {
            gpio_write(&gpios[i], 1);
            sleep(1);
            gpio_write(&gpios[i], 0);
            sleep(1);
        }
    }

    return 0;
}

static int gpio_waiting_thread(void *arg) {
    gpio_test_t* gpio_test = arg;
    gpio_protocol_t* gpios = gpio_test->gpios;
    while(1) {
        zxlogf(INFO, "Waiting for GPIO Test Input Interrupt\n");
        zx_status_t status = zx_interrupt_wait(gpio_test->inth, NULL);
        if (status != ZX_OK) {
            zxlogf(ERROR, "gpio_waiting_thread: zx_interrupt_wait failed %d\n", status);
            return -1;
        }
        zxlogf(INFO, "Received GPIO Test Input Interrupt\n");
        uint8_t out;
        gpio_read(&gpios[GPIO_LED], &out);
        gpio_write(&gpios[GPIO_LED], !out);
        sleep(1);
    }
}

// test thread that cycles runs tests for GPIO interrupts
static int gpio_interrupt_test(void *arg) {
    gpio_test_t* gpio_test = arg;
    gpio_protocol_t* gpios = gpio_test->gpios;

    if (gpio_config_in(&gpios[GPIO_BUTTON], GPIO_PULL_DOWN) != ZX_OK) {
        zxlogf(ERROR, "gpio_interrupt_test: gpio_config failed for gpio %u \n", GPIO_BUTTON);
        return -1;
    }

    if (gpio_get_interrupt(&gpios[GPIO_BUTTON],
                           ZX_INTERRUPT_MODE_EDGE_HIGH, &gpio_test->inth) != ZX_OK) {
        zxlogf(ERROR, "gpio_interrupt_test: gpio_get_interrupt failed for gpio %u\n", GPIO_BUTTON);
        return -1;
    }

    thrd_create_with_name(&gpio_test->wait, gpio_waiting_thread, gpio_test, "gpio_waiting_thread");
    return 0;
}

// test thread that checks for gpio inputs
static int gpio_test_in(void *arg) {
    gpio_test_t* gpio_test = arg;
    gpio_protocol_t* gpios = gpio_test->gpios;

    if (gpio_config_in(&gpios[GPIO_BUTTON], GPIO_NO_PULL) != ZX_OK) {
        zxlogf(ERROR, "gpio_interrupt_test: gpio_config failed for gpio %u \n", GPIO_BUTTON);
        return -1;
    }

    uint8_t out;
    while (!gpio_test->done) {
        gpio_read(&gpios[GPIO_BUTTON], &out);
        if (out) {
            zxlogf(INFO, "READ GPIO_BUTTON %u\n",out);
            sleep(2);
        }
    }
    return 0;
}

static zx_status_t gpio_test_bind(void* ctx, zx_device_t* parent) {
    gpio_test_t* gpio_test = calloc(1, sizeof(gpio_test_t));
    if (!gpio_test) {
        return ZX_ERR_NO_MEMORY;
    }

    pdev_protocol_t pdev;
    if (device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev) != ZX_OK) {
        free(gpio_test);
        return ZX_ERR_NOT_SUPPORTED;
    }

    pdev_device_info_t  info;
    if (pdev_get_device_info(&pdev, &info) != ZX_OK) {
        free(gpio_test);
        return ZX_ERR_NOT_SUPPORTED;
    }
    gpio_test->gpio_count = info.gpio_count;
    gpio_test->gpios = calloc(info.gpio_count, sizeof(*gpio_test->gpios));
    if (!gpio_test->gpios) {
        free(gpio_test);
        return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < info.gpio_count; i++) {
        size_t actual;
        zx_status_t status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, i, &gpio_test->gpios[i],
                                               sizeof(gpio_test->gpios[i]), &actual);
        if (status != ZX_OK) {
            free(gpio_test->gpios);
            free(gpio_test);
            return status;
        }
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "gpio-test",
        .ctx = gpio_test,
        .ops = &gpio_test_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_status_t status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        free(gpio_test);
        return status;
    }

    thrd_create_with_name(&gpio_test->thread, gpio_test_thread, gpio_test, "gpio_test_thread");
    thrd_create_with_name(&gpio_test->thread, gpio_interrupt_test, gpio_test, "gpio_interrupt_test");
    return ZX_OK;
}

static zx_driver_ops_t gpio_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gpio_test_bind,
};

ZIRCON_DRIVER_BEGIN(gpio_test, gpio_test_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GPIO_TEST),
ZIRCON_DRIVER_END(gpio_test)
