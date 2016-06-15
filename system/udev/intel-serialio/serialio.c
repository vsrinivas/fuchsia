// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/protocol/char.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <mxu/list.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <stdio.h>

#include <intel_broadwell_serialio/serialio.h>

static mx_status_t intel_broadwell_serialio_probe(mx_driver_t* drv,
                                                  mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    const pci_config_t* pci_config;
    mx_handle_t config_handle = pci->get_config(dev, &pci_config);

    if (config_handle < 0)
        return config_handle;

    mx_status_t res;
    if ((pci_config->vendor_id == INTEL_VID) &&
        ((pci_config->device_id == INTEL_BROADWELL_SERIALIO_DMA_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_I2C0_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_I2C1_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_SDIO_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_SPI0_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_SPI1_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_UART0_DID) ||
         (pci_config->device_id == INTEL_BROADWELL_SERIALIO_UART1_DID))) {
        res = NO_ERROR;
    } else {
        res = ERR_NOT_SUPPORTED;
    }

    _magenta_handle_close(config_handle);
    return res;
}

static mx_status_t intel_broadwell_serialio_bind(mx_driver_t* drv,
                                                 mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    const pci_config_t* pci_config;
    mx_handle_t config_handle = pci->get_config(dev, &pci_config);

    if (config_handle < 0)
        return config_handle;

    mx_status_t res;
    switch (pci_config->device_id) {
    case INTEL_BROADWELL_SERIALIO_DMA_DID:
        res = intel_broadwell_serialio_bind_dma(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_I2C0_DID:
        res = intel_broadwell_serialio_bind_i2c(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_I2C1_DID:
        res = intel_broadwell_serialio_bind_i2c(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_SDIO_DID:
        res = intel_broadwell_serialio_bind_sdio(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_SPI0_DID:
        res = intel_broadwell_serialio_bind_spi(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_SPI1_DID:
        res = intel_broadwell_serialio_bind_spi(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_UART0_DID:
        res = intel_broadwell_serialio_bind_uart(drv, dev);
        break;
    case INTEL_BROADWELL_SERIALIO_UART1_DID:
        res = intel_broadwell_serialio_bind_uart(drv, dev);
        break;
    default:
        res = ERR_NOT_SUPPORTED;
        break;
    }

    _magenta_handle_close(config_handle);
    return res;
}

static mx_driver_binding_t binding = {
    .protocol_id = MX_PROTOCOL_PCI,
};

mx_driver_t _intel_broadwell_serialio BUILTIN_DRIVER = {
    .name = "intel_broadwell_serialio",
    .ops = {
        .probe = intel_broadwell_serialio_probe,
        .bind = intel_broadwell_serialio_bind,
    },
    .binding = &binding,
    .binding_count = 1,
};
