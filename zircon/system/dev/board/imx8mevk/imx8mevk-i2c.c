// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/imx8m/imx8m-hw.h>
#include <soc/imx8m/imx8m-iomux.h>

#include "imx8mevk.h"

static const pbus_mmio_t imx_i2c_mmios[] = {
    {
        .base = IMX8M_I2C1_BASE,
        .length = IMX8M_I2C1_LENGTH,
    },
};

const pbus_dev_t imx_i2c_dev = {
    .name = "imx8mevk-i2c",
    .vid = PDEV_VID_NXP,
    .pid = PDEV_PID_IMX8MEVK,
    .did = PDEV_DID_IMX_I2C,
    .mmio_list = imx_i2c_mmios,
    .mmio_count = countof(imx_i2c_mmios),
};

zx_status_t imx_i2c_init(imx8mevk_bus_t* bus) {
    // TODO(andresoportus): clocks and pin mux setup
    zx_status_t status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_I2C_IMPL, &imx_i2c_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "imx_i2c_init could not add dev: %d\n", status);
        return status;
    }
    return ZX_OK;
}
