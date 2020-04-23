// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hikey960.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/iommu.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-regs.h>

#include "hikey960-hw.h"

static zx_status_t hikey960_enable_ldo3(hikey960_t* hikey) {
  writel(LDO3_ENABLE_BIT, hikey->pmu_ssio.vaddr + LDO3_ENABLE_REG);
  return ZX_OK;
}

static void hikey960_mmio_release(hikey960_t* hikey) {
  mmio_buffer_release(&hikey->usb3otg_bc);
  mmio_buffer_release(&hikey->peri_crg);
  mmio_buffer_release(&hikey->pctrl);
  mmio_buffer_release(&hikey->iomg_pmx4);
  mmio_buffer_release(&hikey->pmu_ssio);
  mmio_buffer_release(&hikey->iomcu);
  mmio_buffer_release(&hikey->ufs_sctrl);
}

static zx_status_t hikey960_init(hikey960_t* hikey, zx_handle_t resource) {
  zx_status_t status;
  if ((status = mmio_buffer_init_physical(&hikey->usb3otg_bc, MMIO_USB3OTG_BC_BASE,
                                          MMIO_USB3OTG_BC_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
      (status =
           mmio_buffer_init_physical(&hikey->peri_crg, MMIO_PERI_CRG_BASE, MMIO_PERI_CRG_LENGTH,
                                     resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
      (status = mmio_buffer_init_physical(&hikey->pctrl, MMIO_PCTRL_BASE, MMIO_PCTRL_LENGTH,
                                          resource, ZX_CACHE_POLICY_UNCACHED_DEVICE) != ZX_OK) ||
      (status =
           mmio_buffer_init_physical(&hikey->iomg_pmx4, MMIO_IOMG_PMX4_BASE, MMIO_IOMG_PMX4_LENGTH,
                                     resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
      (status =
           mmio_buffer_init_physical(&hikey->pmu_ssio, MMIO_PMU_SSI0_BASE, MMIO_PMU_SSI0_LENGTH,
                                     resource, ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
      (status = mmio_buffer_init_physical(&hikey->iomcu, MMIO_IOMCU_CONFIG_BASE,
                                          MMIO_IOMCU_CONFIG_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK ||
      (status = mmio_buffer_init_physical(&hikey->ufs_sctrl, MMIO_UFS_SYS_CTRL_BASE,
                                          MMIO_UFS_SYS_CTRL_LENGTH, resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE)) != ZX_OK) {
    goto fail;
  }

  status = hikey960_ufs_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_i2c1_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_enable_ldo3(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_i2c_pinmux(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  return ZX_OK;

fail:
  zxlogf(ERROR, "hikey960_init failed %d", status);
  hikey960_mmio_release(hikey);
  return status;
}

static void hikey960_release(void* ctx) {
  hikey960_t* hikey = ctx;

  hikey960_mmio_release(hikey);
  zx_handle_close(hikey->bti_handle);
  free(hikey);
}

static zx_protocol_device_t hikey960_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = hikey960_release,
};

static int hikey960_start_thread(void* arg) {
  hikey960_t* hikey = arg;
  zx_status_t status;

  status = hikey960_sysmem_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_gpio_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  status = hikey960_i2c_init(hikey);
  if (status != ZX_OK) {
    goto fail;
  }

  // must be after hikey960_i2c_init
  status = hikey960_dsi_init(hikey);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_dsi_init failed");
  }

  if ((status = hikey960_add_devices(hikey)) != ZX_OK) {
    zxlogf(ERROR, "hikey960_bind: hikey960_add_devices failed!");
  }

  return ZX_OK;

fail:
  zxlogf(ERROR, "hikey960_start_thread failed, not all devices have been initialized");
  return status;
}

static zx_status_t hikey960_bind(void* ctx, zx_device_t* parent) {
  hikey960_t* hikey = calloc(1, sizeof(hikey960_t));
  if (!hikey) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &hikey->pbus);
  if (status != ZX_OK) {
    free(hikey);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // get dummy IOMMU implementation in the platform bus
  iommu_protocol_t iommu;
  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_bind: could not get ZX_PROTOCOL_IOMMU");
    goto fail;
  }
  status = iommu_get_bti(&iommu, 0, BTI_BOARD, &hikey->bti_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_bind: iommu_get_bti failed: %d", status);
    return status;
  }

  hikey->parent = parent;

  // TODO(voydanoff) get from platform bus driver somehow
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_handle_t resource = get_root_resource();
  status = hikey960_init(hikey, resource);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_bind: hikey960_init failed %d", status);
    goto fail;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "hikey960",
      .ctx = hikey,
      .ops = &hikey960_device_protocol,
      // nothing should bind to this device
      // all interaction will be done via the pbus_interface_protocol_t
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  status = device_add(parent, &args, NULL);
  if (status != ZX_OK) {
    goto fail;
  }

  thrd_t t;
  int thrd_rc = thrd_create_with_name(&t, hikey960_start_thread, hikey, "hikey960_start_thread");
  if (thrd_rc != thrd_success) {
    status = thrd_status_to_zx_status(thrd_rc);
    goto fail;
  }
  return ZX_OK;

fail:
  zxlogf(ERROR, "hikey960_bind failed %d", status);
  hikey960_release(hikey);
  return status;
}

static zx_driver_ops_t hikey960_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hikey960_bind,
};

ZIRCON_DRIVER_BEGIN(hikey960, hikey960_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960), ZIRCON_DRIVER_END(hikey960)
