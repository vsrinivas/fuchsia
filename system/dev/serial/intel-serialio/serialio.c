// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <zircon/listnode.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <stdio.h>

#include <intel-serialio/serialio.h>

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
    case INTEL_WILDCAT_POINT_SERIALIO_I2C0_DID:
    case INTEL_WILDCAT_POINT_SERIALIO_I2C1_DID:
    case INTEL_SUNRISE_POINT_SERIALIO_I2C0_DID:
    case INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID:
    case INTEL_SUNRISE_POINT_SERIALIO_I2C2_DID:
    case INTEL_SUNRISE_POINT_SERIALIO_I2C3_DID:
        res = intel_serialio_bind_i2c(dev);
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

// clang-format off
ZIRCON_DRIVER_BEGIN(intel_serialio, intel_serialio_driver_ops, "zircon", "0.1", 14)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_DMA_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_I2C0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_I2C1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_SDIO_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_SPI0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_SPI1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_UART0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_UART1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C2_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C3_DID),
ZIRCON_DRIVER_END(intel_serialio)
