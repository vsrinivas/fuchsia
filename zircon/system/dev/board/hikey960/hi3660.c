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
#include <gpio/pl061/pl061.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/assert.h>

#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-regs.h>
#include <hw/reg.h>

#include "hikey960.h"

zx_status_t hi3660_enable_ldo3(hi3660_t* hi3660) {
    writel(LDO3_ENABLE_BIT, hi3660->pmu_ssio.vaddr + LDO3_ENABLE_REG);
    return ZX_OK;
}

zx_status_t hi3660_init(hi3660_t* hi3660, zx_handle_t resource) {
    list_initialize(&hi3660->gpios);

    zx_status_t status;
    if ((status = mmio_buffer_init_physical(&hi3660->usb3otg_bc, MMIO_USB3OTG_BC_BASE,
                                          MMIO_USB3OTG_BC_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = mmio_buffer_init_physical(&hi3660->peri_crg, MMIO_PERI_CRG_BASE,
                                           MMIO_PERI_CRG_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = mmio_buffer_init_physical(&hi3660->pctrl, MMIO_PCTRL_BASE, MMIO_PCTRL_LENGTH,
                                           resource, ZX_CACHE_POLICY_UNCACHED_DEVICE) != ZX_OK) ||
         (status = mmio_buffer_init_physical(&hi3660->iomg_pmx4, MMIO_IOMG_PMX4_BASE,
                                           MMIO_IOMG_PMX4_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = mmio_buffer_init_physical(&hi3660->pmu_ssio, MMIO_PMU_SSI0_BASE,
                                           MMIO_PMU_SSI0_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = mmio_buffer_init_physical(&hi3660->iomcu, MMIO_IOMCU_CONFIG_BASE,
                                           MMIO_IOMCU_CONFIG_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
         (status = mmio_buffer_init_physical(&hi3660->ufs_sctrl, MMIO_UFS_SYS_CTRL_BASE,
                                           MMIO_UFS_SYS_CTRL_LENGTH, resource,
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK) {
        goto fail;
    }

    status = hi3660_gpio_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }
    status = hi3660_usb_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }

    status = hi3660_ufs_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }

    status = hi3660_i2c1_init(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }

    status = hi3660_enable_ldo3(hi3660);
    if (status != ZX_OK) {
      goto fail;
    }

    status = hi3660_i2c_pinmux(hi3660);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "hi3660_init failed %d\n", status);
    hi3660_release(hi3660);
    return status;
}

void hi3660_release(hi3660_t* hi3660) {
    hi3660_gpio_release(hi3660);
    mmio_buffer_release(&hi3660->usb3otg_bc);
    mmio_buffer_release(&hi3660->peri_crg);
    mmio_buffer_release(&hi3660->pctrl);
    mmio_buffer_release(&hi3660->iomg_pmx4);
    mmio_buffer_release(&hi3660->pmu_ssio);
    mmio_buffer_release(&hi3660->iomcu);
    mmio_buffer_release(&hi3660->ufs_sctrl);
}
