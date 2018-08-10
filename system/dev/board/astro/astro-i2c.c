// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>

#include "astro.h"

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = S905D2_I2C_AO_0_BASE,
        .length = 0x20,
    },
    {
        .base = S905D2_I2C2_BASE,
        .length = 0x20,
    },
    {
        .base = S905D2_I2C3_BASE,
        .length = 0x20,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = S905D2_I2C_AO_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
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

    //i2c_ao_0
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOAO(2), 1);
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOAO(3), 1);
    //i2c2
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOZ(14), 3);
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOZ(15), 3);
    //i2c3
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOA(14), 2);
    gpio_set_alt_function(&bus->gpio, S905D2_GPIOA(15), 2);

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
