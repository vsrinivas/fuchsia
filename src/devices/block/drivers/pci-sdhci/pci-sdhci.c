// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/sdhci/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/pci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>

#include "src/devices/block/drivers/pci-sdhci/pci-sdhci-bind.h"

#define HOST_CONTROL1_OFFSET 0x28
#define SDHCI_EMMC_HW_RESET (1 << 12)

typedef struct pci_sdhci_device {
  zx_device_t* zxdev;
  pci_protocol_t pci;

  MMIO_PTR volatile uint8_t* regs;
  mmio_buffer_t mmio;
  zx_handle_t bti_handle;
} pci_sdhci_device_t;

static zx_status_t pci_sdhci_get_interrupt(void* ctx, zx_handle_t* handle_out) {
  pci_sdhci_device_t* dev = ctx;
  // select irq mode
  pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_DISABLED;
  zx_status_t status = pci_configure_interrupt_mode(&dev->pci, 1, &mode);
  if (status != ZX_OK) {
    zxlogf(ERROR, "error setting irq mode: %s", zx_status_get_string(status));
    return status;
  }

  // get irq handle
  status = pci_map_interrupt(&dev->pci, 0, handle_out);
  if (status != ZX_OK) {
    zxlogf(ERROR, "error getting irq handle: %s", zx_status_get_string(status));
  }
  return status;
}

static zx_status_t pci_sdhci_get_mmio(void* ctx, zx_handle_t* out, zx_off_t* out_offset) {
  pci_sdhci_device_t* dev = ctx;
  if (dev->regs == NULL) {
    zx_status_t status =
        pci_map_bar_buffer(&dev->pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &dev->mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "error mapping register window: %s", zx_status_get_string(status));
      return status;
    }
    dev->regs = dev->mmio.vaddr;
  }
  *out_offset = dev->mmio.offset;
  return zx_handle_duplicate(dev->mmio.vmo, ZX_RIGHT_SAME_RIGHTS, out);
}

static zx_status_t pci_sdhci_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
  pci_sdhci_device_t* dev = ctx;
  if (dev->bti_handle == ZX_HANDLE_INVALID) {
    zx_status_t st = pci_get_bti(&dev->pci, index, &dev->bti_handle);
    if (st != ZX_OK) {
      return st;
    }
  }
  return zx_handle_duplicate(dev->bti_handle, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static uint32_t pci_sdhci_get_base_clock(void* ctx) { return 0; }

static uint64_t pci_sdhci_get_quirks(void* ctx, uint64_t* out_dma_boundary_alignment) {
  *out_dma_boundary_alignment = 0;
  return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;
}

static void pci_sdhci_hw_reset(void* ctx) {
  pci_sdhci_device_t* dev = ctx;
  if (!dev->regs) {
    return;
  }
  MMIO_PTR volatile uint32_t* const ctrl1 =
      (MMIO_PTR volatile uint32_t*)(dev->regs + HOST_CONTROL1_OFFSET);
  uint32_t val = MmioRead32(ctrl1);
  val |= SDHCI_EMMC_HW_RESET;
  MmioWrite32(val, ctrl1);
  // minimum is 1us but wait 9us for good measure
  zx_nanosleep(zx_deadline_after(ZX_USEC(9)));
  val &= ~SDHCI_EMMC_HW_RESET;
  MmioWrite32(val, ctrl1);
  // minimum is 200us but wait 300us for good measure
  zx_nanosleep(zx_deadline_after(ZX_USEC(300)));
}

static sdhci_protocol_ops_t pci_sdhci_sdhci_proto = {
    .get_interrupt = pci_sdhci_get_interrupt,
    .get_mmio = pci_sdhci_get_mmio,
    .get_bti = pci_sdhci_get_bti,
    .get_base_clock = pci_sdhci_get_base_clock,
    .get_quirks = pci_sdhci_get_quirks,
    .hw_reset = pci_sdhci_hw_reset,
};

static void pci_sdhci_unbind(void* ctx) {
  pci_sdhci_device_t* dev = ctx;
  device_unbind_reply(dev->zxdev);
}

static void pci_sdhci_release(void* ctx) {
  pci_sdhci_device_t* dev = ctx;
  mmio_buffer_release(&dev->mmio);
  zx_handle_close(dev->bti_handle);
  free(dev);
}

static zx_protocol_device_t pci_sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = pci_sdhci_unbind,
    .release = pci_sdhci_release,
};

static zx_status_t pci_sdhci_bind(void* ctx, zx_device_t* parent) {
  zxlogf(DEBUG, "bind");
  pci_sdhci_device_t* dev = calloc(1, sizeof(pci_sdhci_device_t));
  if (!dev) {
    zxlogf(ERROR, "out of memory");
    return ZX_ERR_NO_MEMORY;
  }

  // TODO(fxbug.dev/93333): Remove this once DFv2 has stabilised.
  bool is_dfv2 = device_is_dfv2(parent);

  zx_status_t status = ZX_OK;
  if (is_dfv2) {
    status = device_get_protocol(parent, ZX_PROTOCOL_PCI, &dev->pci);
  } else {
    status = device_get_fragment_protocol(parent, "pci", ZX_PROTOCOL_PCI, &dev->pci);
  }
  if (status != ZX_OK) {
    goto fail;
  }

  status = dev->pci.ops->set_bus_mastering(dev->pci.ctx, true);
  if (status < 0) {
    zxlogf(ERROR, "error in enable bus master: %s", zx_status_get_string(status));
    goto fail;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "pci-sdhci",
      .ctx = dev,
      .ops = &pci_sdhci_device_proto,
      .proto_id = ZX_PROTOCOL_SDHCI,
      .proto_ops = &pci_sdhci_sdhci_proto,
  };

  status = device_add(parent, &args, &dev->zxdev);
  if (status != ZX_OK) {
    goto fail;
  }

  return ZX_OK;
fail:
  free(dev);
  return status;
}

static zx_driver_ops_t pci_sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci_sdhci_bind,
};

ZIRCON_DRIVER(pci_sdhci, pci_sdhci_driver_ops, "zircon", "0.1");
