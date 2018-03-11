// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/hi3660/hi3660-hw.h>

#include <limits.h>

#include "hikey960.h"

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = MMIO_I2C0_BASE,
        .length = MMIO_I2C0_LENGTH,
    },
    {
        .base = MMIO_I2C1_BASE,
        .length = MMIO_I2C1_LENGTH,
    },
    {
        .base = MMIO_I2C2_BASE,
        .length = MMIO_I2C2_LENGTH,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = IRQ_IOMCU_I2C0,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IRQ_IOMCU_I2C1,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = IRQ_IOMCU_I2C2,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_dev_t i2c_dev = {
    .name = "i2c",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DW_I2C,
    .mmios = i2c_mmios,
    .mmio_count = countof(i2c_mmios),
    .irqs = i2c_irqs,
    .irq_count = countof(i2c_irqs),
};

zx_status_t hikey960_i2c_init(hikey960_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &i2c_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hikey960_i2c_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_I2C_IMPL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hikey960_i2c_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    return device_get_protocol(bus->parent, ZX_PROTOCOL_I2C_IMPL, &bus->i2c);
}
