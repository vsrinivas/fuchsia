// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660.h>
#include <soc/hi3660/hi3660-regs.h>
#include <soc/hi3660/hi3660-pinmux.h>
#include <ddk/debug.h>
#include <stdio.h>

#define I2C1_ENABLE_REG_OFFSET          0x10
#define I2C1_ENABLE_REG_BIT             0x4

#define MMIO_IOCFG_PMX9_OFFSET          0x800

zx_status_t hi3660_i2c1_init(hi3660_t* hi3660) {
    volatile void* iomcu = io_buffer_virt(&hi3660->iomcu) + I2C1_ENABLE_REG_OFFSET;
    uint32_t temp;

    temp = readl(iomcu + CLKGATE_SEPERATED_ENABLE);
    temp |= (1 << I2C1_ENABLE_REG_BIT);
    writel(temp, iomcu + CLKGATE_SEPERATED_ENABLE);
    readl(iomcu + CLKGATE_SEPERATED_STATUS); // need to read back status to ensure enable occurs

    return ZX_OK;
}

zx_status_t hi3660_i2c_pinmux(hi3660_t* hi3660) {
    // setup i2c0 and i2c1 pin control first
    volatile void* iomg_pmx4 = io_buffer_virt(&hi3660->iomg_pmx4);
    volatile void* iocfg_pmx9 = iomg_pmx4 + MMIO_IOCFG_PMX9_OFFSET;

    writel(MUX_M1, iomg_pmx4 + I2C0_SCL_MUX_OFFSET); // I2C0_SCL
    writel(MUX_M1, iomg_pmx4 + I2C0_SDA_MUX_OFFSET); // I2C0_SDA
    writel(MUX_M1, iomg_pmx4 + I2C1_SCL_MUX_OFFSET); // I2C1_SCL
    writel(MUX_M1, iomg_pmx4 + I2C1_SDA_MUX_OFFSET); // I2C1_SDA

    // configure the pins (pu/pd, drive strength)
    writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C0_SCL_CFG_OFFSET);
    writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C0_SDA_CFG_OFFSET);
    writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C1_SCL_CFG_OFFSET);
    writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C1_SDA_CFG_OFFSET);

    return ZX_OK;


}