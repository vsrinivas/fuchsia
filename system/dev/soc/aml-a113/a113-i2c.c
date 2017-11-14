// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#include "a113-bus.h"
#include "a113-hw.h"
#include "aml-i2c.h"

// turn this on to enable Gauss accelerometer test driver
//#define I2C_TEST 1

static zx_status_t a113_i2c_transact(void* ctx, const void* write_buf, size_t write_length,
                                     size_t read_length, i2c_complete_cb complete_cb,
                                     void* cookie) {
    if (read_length > AML_I2C_MAX_TRANSFER || write_length > AML_I2C_MAX_TRANSFER) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    aml_i2c_connection_t* connection = ctx;
    return aml_i2c_wr_rd_async(connection, write_buf, write_length, read_length, complete_cb,
                               cookie);
}

static zx_status_t a113_i2c_set_bitrate(void* ctx, uint32_t bitrate) {
    // TODO(hollande,voydanoff) implement this
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t a113_i2c_get_max_transfer_size(void* ctx, size_t* out_size) {
    *out_size = AML_I2C_MAX_TRANSFER;
    return ZX_OK;
}

static void a113_i2c_channel_release(void* ctx) {
    aml_i2c_connection_t* connection = ctx;
    aml_i2c_release(connection);
}

static i2c_channel_ops_t a113_i2c_channel_ops = {
    .transact = a113_i2c_transact,
    .set_bitrate = a113_i2c_set_bitrate,
    .get_max_transfer_size = a113_i2c_get_max_transfer_size,
    .channel_release = a113_i2c_channel_release,
};

static zx_status_t a113_i2c_get_channel(void* ctx, uint32_t channel_id, i2c_channel_t* channel) {
    // i2c_get_channel is only used via by platform devices
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t a113_i2c_get_channel_by_address(void* ctx, uint32_t bus_id, uint16_t address,
                                                   i2c_channel_t* channel) {
    if (bus_id >= AML_I2C_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }
    a113_bus_t* bus = ctx;
    aml_i2c_dev_t* dev = bus->i2c_devs[bus_id];
     if (!dev) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    aml_i2c_connection_t* connection;
    uint32_t address_bits = 7;

    if ((address & I2C_10_BIT_ADDR_MASK) == I2C_10_BIT_ADDR_MASK) {
        address_bits = 10;
        address &= ~I2C_10_BIT_ADDR_MASK;
    }

    zx_status_t status = aml_i2c_connect(&connection, dev, address, address_bits);
    if (status != ZX_OK) {
        return status;
    }

    channel->ops = &a113_i2c_channel_ops;
    channel->ctx = connection;
    return ZX_OK;
}

static i2c_protocol_ops_t i2c_ops = {
    .get_channel = a113_i2c_get_channel,
    .get_channel_by_address = a113_i2c_get_channel_by_address,
};

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

zx_status_t a113_i2c_init(a113_bus_t* bus) {
    // Gauss only uses I2C_A and I2C_B
    zx_status_t status = aml_i2c_init(&bus->i2c_devs[AML_I2C_A], AML_I2C_A);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init: aml_i2c_init failed %d\n", status);
        return status;
    }
    status = aml_i2c_init(&bus->i2c_devs[AML_I2C_B], AML_I2C_B);
    if (status != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init: aml_i2c_init failed %d\n", status);
        return status;
    }

    bus->i2c.ops = &i2c_ops;
    bus->i2c.ctx = bus;

#if I2C_TEST
    if ((status = pbus_device_add(&bus->pbus, &i2c_test_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "a113_i2c_init could not add i2c_test_dev: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
