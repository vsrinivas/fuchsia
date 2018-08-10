// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include <limits.h>

#include "aml.h"

static const pbus_mmio_t i2c_mmios[] = {
    {
        // AML_I2C_A
        .base = 0xc1108500,
        .length = 0x20,
    },
    {
        // AML_I2C_B
        .base = 0xc11087c0,
        .length = 0x20,
    },
    {
        // AML_I2C_C
        .base = 0xc11087e0,
        .length = 0x20,
    },
/*
    {
        // AML_I2C_D
        .base = 0xc1108d20,
        .length = 0x20,
    },
*/
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = 21 + 32,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = 214 + 32,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = 215 + 32,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
/*
    {
        .irq = 39 + 32,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
*/
};

static const pbus_dev_t i2c_dev = {
    .name = "i2c",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_I2C,
    .mmios = i2c_mmios,
    .mmio_count = countof(i2c_mmios),
    .irqs = i2c_irqs,
    .irq_count = countof(i2c_irqs),
};

zx_status_t aml_i2c_init(aml_bus_t* bus) {
    // setup pinmux for our I2C busses
    // I2C_A and I2C_B are exposed on the 40 pin header and I2C_C on the FPC connector
    gpio_set_alt_function(&bus->gpio, S912_I2C_SDA_A, S912_I2C_SDA_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_I2C_SCK_A, S912_I2C_SCK_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_I2C_SDA_B, S912_I2C_SDA_B_FN);
    gpio_set_alt_function(&bus->gpio, S912_I2C_SCK_B, S912_I2C_SCK_B_FN);
    gpio_set_alt_function(&bus->gpio, S912_I2C_SDA_C, S912_I2C_SDA_C_FN);
    gpio_set_alt_function(&bus->gpio, S912_I2C_SCK_C, S912_I2C_SCK_C_FN);

    zx_status_t status = pbus_device_add(&bus->pbus, &i2c_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_I2C_IMPL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
