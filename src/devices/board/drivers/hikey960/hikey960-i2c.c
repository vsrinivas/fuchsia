// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-pinmux.h>
#include <soc/hi3660/hi3660-regs.h>

#include "hikey960.h"

#define I2C1_ENABLE_REG_OFFSET 0x10
#define I2C1_ENABLE_REG_BIT 0x4

#define MMIO_IOCFG_PMX9_OFFSET 0x800

zx_status_t hikey960_i2c1_init(hikey960_t* hikey) {
  volatile void* iomcu = hikey->iomcu.vaddr + I2C1_ENABLE_REG_OFFSET;
  uint32_t temp;

  temp = readl(iomcu + CLKGATE_SEPARATED_ENABLE);
  temp |= (1 << I2C1_ENABLE_REG_BIT);
  writel(temp, iomcu + CLKGATE_SEPARATED_ENABLE);
  readl(iomcu + CLKGATE_SEPARATED_STATUS);  // need to read back status to ensure enable occurs

  return ZX_OK;
}

zx_status_t hikey960_i2c_pinmux(hikey960_t* hikey) {
  // setup i2c0 and i2c1 pin control first
  volatile void* iomg_pmx4 = hikey->iomg_pmx4.vaddr;
  volatile void* iocfg_pmx9 = iomg_pmx4 + MMIO_IOCFG_PMX9_OFFSET;

  writel(MUX_M1, iomg_pmx4 + I2C0_SCL_MUX_OFFSET);  // I2C0_SCL
  writel(MUX_M1, iomg_pmx4 + I2C0_SDA_MUX_OFFSET);  // I2C0_SDA
  writel(MUX_M1, iomg_pmx4 + I2C1_SCL_MUX_OFFSET);  // I2C1_SCL
  writel(MUX_M1, iomg_pmx4 + I2C1_SDA_MUX_OFFSET);  // I2C1_SDA

  // configure the pins (pu/pd, drive strength)
  writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C0_SCL_CFG_OFFSET);
  writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C0_SDA_CFG_OFFSET);
  writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C1_SCL_CFG_OFFSET);
  writel(DRIVE7_02MA | PULL_UP, iocfg_pmx9 + I2C1_SDA_CFG_OFFSET);

  return ZX_OK;
}

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
    .mmio_list = i2c_mmios,
    .mmio_count = countof(i2c_mmios),
    .irq_list = i2c_irqs,
    .irq_count = countof(i2c_irqs),
};

zx_status_t hikey960_i2c_init(hikey960_t* bus) {
  zx_status_t status = pbus_device_add(&bus->pbus, &i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_i2c_init: pbus_device_add failed: %d", status);
    return status;
  }

  return ZX_OK;
}
