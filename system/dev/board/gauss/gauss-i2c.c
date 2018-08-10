// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-a113/a113-hw.h>

#include <limits.h>

#include "gauss.h"

static const pbus_mmio_t i2c_mmios[] = {
    {
        // AML_I2C_A
        .base = 0xffd1f000,
        .length = PAGE_SIZE,
    },
    {
        // AML_I2C_B
        .base = 0xffd1e000,
        .length = PAGE_SIZE,
    },
 // Gauss only uses I2C_A and I2C_B
/*
     {
        // AML_I2C_C
        .base = 0xffd1d000,
        .length = PAGE_SIZE,
    },
    {
        // AML_I2C_D
        .base = 0xffd1e000,
        .length = 0xffd1c000,
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
 // Gauss only uses I2C_A and I2C_B
/*
    {
        .irq = 215 + 32,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
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

zx_status_t gauss_i2c_init(gauss_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &i2c_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_i2c_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_I2C_IMPL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "gauss_i2c_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
