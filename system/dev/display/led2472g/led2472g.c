// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-defs.h>

static void i2c_complete(zx_status_t status, const uint8_t* data, void* cookie) {
    if (status != ZX_OK) {
        zxlogf(ERROR, "led2472g i2c_complete error: %d\n", status);
    }
}

typedef struct {
    zx_device_t* zdev;
    i2c_protocol_t i2c;
    thrd_t thread;
    bool done;
    bool override;
} led2472g_t;

static void led2472g_release(void* ctx) {
    led2472g_t* bus = ctx;
    bus->done = true;
    thrd_join(bus->thread, NULL);
    free(bus);
}

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static zx_status_t led2472g_write(void* ctx, const void* buf, size_t count,
                                  zx_off_t off, size_t* actual) {
    led2472g_t* led2472g = ctx;

    led2472g->override = true;

    const char* inbuf = buf;
    char i2c_buf[3 * 8 * 8 + 1] = {0x0};
    for (size_t i = 0; i < MIN(count, sizeof(i2c_buf)); i++) {
        i2c_buf[i] = inbuf[i] >> 3;
    }
    i2c_transact(&led2472g->i2c, 0, i2c_buf, sizeof(i2c_buf), 0, i2c_complete, NULL);

    return ZX_OK;
}

static zx_protocol_device_t led2472g_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = led2472g_release,
    .write = led2472g_write,
};

static int led2472g_thread(void* arg) {
    led2472g_t* led2472g = arg;

    char colors[][3] = {
        {0xFF, 0x00, 0x00}, // #FF0000
        {0xFF, 0x7F, 0x00}, // #FF7F00
        {0xFF, 0xFF, 0x00}, // #FFFF00
        {0x00, 0xFF, 0x00}, // #00FF00
        {0x00, 0x00, 0xFF}, // #0000FF
        {0x8B, 0x00, 0xFF}, // #8B00FF
    };
    int cix = 0;
    int st = 0;
    float bright = 1.0;
    float dim = -0.01;
    while (!led2472g->done && !led2472g->override) {
        cix = st;
        char buf[3 * 8 * 8 + 1] = {0x0};
        for (int y = 0; y < 8; y++) {
            char r = (char)(colors[cix][0] * bright) >> 3;
            char g = (char)(colors[cix][1] * bright) >> 3;
            char b = (char)(colors[cix][2] * bright) >> 3;
            for (int x = 0; x < 8; x++) {
                buf[1 + x + 8 * 0 + 3 * 8 * y] = r;
                buf[1 + x + 8 * 1 + 3 * 8 * y] = g; // g
                buf[1 + x + 8 * 2 + 3 * 8 * y] = b; // b
            }
            cix += 1;
            cix %= countof(colors);
        }
        i2c_transact(&led2472g->i2c, 0, buf, sizeof(buf), 0, i2c_complete, NULL);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
        st += 1;
        st %= countof(colors);
        bright += dim;
        if (bright <= 0.0 || bright >= 1.0) {
            dim = -dim;
        }
    }

    return ZX_OK;
}

static zx_status_t led2472g_bind(void* ctx, zx_device_t* parent) {
    led2472g_t* led2472g = calloc(1, sizeof(led2472g_t));
    if (led2472g == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_I2C, &led2472g->i2c) != ZX_OK) {
        free(led2472g);
        return ZX_ERR_NOT_SUPPORTED;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "led2472g",
        .ctx = led2472g,
        .ops = &led2472g_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_status_t status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        free(led2472g);
        return status;
    }

    thrd_create_with_name(&led2472g->thread, led2472g_thread, led2472g, "led2472g_thread");
    return ZX_OK;
}

static zx_driver_ops_t led2472g_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = led2472g_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(led2472g, led2472g_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_LED2472G),
ZIRCON_DRIVER_END(led2472g)
    // clang-format on
