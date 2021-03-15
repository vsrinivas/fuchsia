// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/pci/hw.h>
#include <stdio.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <intel-serialio/serialio.h>

#include "src/devices/serial/drivers/intel-serialio/intel_serialio_bind.h"

static zx_status_t intel_serialio_bind(void* ctx, zx_device_t* dev) {
  pci_protocol_t pci;
  zx_status_t res;

  if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci))
    return ZX_ERR_NOT_SUPPORTED;

  uint16_t device_id;
  pci_config_read16(&pci, PCI_CONFIG_DEVICE_ID, &device_id);

  switch (device_id) {
    case INTEL_WILDCAT_POINT_SERIALIO_DMA_DID:
      res = intel_serialio_bind_dma(dev);
      break;
    case INTEL_WILDCAT_POINT_SERIALIO_SDIO_DID:
      res = intel_serialio_bind_sdio(dev);
      break;
    case INTEL_WILDCAT_POINT_SERIALIO_SPI0_DID:
      res = intel_serialio_bind_spi(dev);
      break;
    case INTEL_WILDCAT_POINT_SERIALIO_SPI1_DID:
      res = intel_serialio_bind_spi(dev);
      break;
    case INTEL_WILDCAT_POINT_SERIALIO_UART0_DID:
      res = intel_serialio_bind_uart(dev);
      break;
    case INTEL_WILDCAT_POINT_SERIALIO_UART1_DID:
      res = intel_serialio_bind_uart(dev);
      break;
    default:
      res = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  return res;
}

static zx_driver_ops_t intel_serialio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_serialio_bind,
};

ZIRCON_DRIVER(intel_serialio, intel_serialio_driver_ops, "zircon", "0.1");
