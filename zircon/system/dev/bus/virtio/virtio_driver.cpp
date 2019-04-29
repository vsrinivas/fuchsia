// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <utility>

#include "backends/pci.h"
#include "block.h"
#include "console.h"
#include "device.h"
#include "ethernet.h"
#include "gpu.h"
#include "input.h"
#include "rng.h"
#include "scsi.h"
#include "socket.h"

static zx_status_t virtio_pci_bind(void* ctx, zx_device_t* bus_device) {
    zx_status_t status;
    pci_protocol_t pci;

    // grab the pci device and configuration to pass to the backend
    if (device_get_protocol(bus_device, ZX_PROTOCOL_PCI, &pci)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_pcie_device_info_t info;
    status = pci_get_device_info(&pci, &info);
    if (status != ZX_OK) {
        return status;
    }

    zx::bti bti;
    status = pci_get_bti(&pci, 0, bti.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    // Due to the similarity between Virtio 0.9.5 legacy devices and Virtio 1.0
    // transitional devices we need to check whether modern capabilities exist.
    // If no vendor capabilities are found then we will default to the legacy
    // interface.
    fbl::unique_ptr<virtio::Backend> backend = nullptr;
    uint8_t offset;
    if (pci_get_first_capability(&pci, PCI_CAP_ID_VENDOR, &offset) == ZX_OK) {
        zxlogf(SPEW, "virtio %02x:%02x.%1x using modern PCI backend\n", info.bus_id, info.dev_id,
               info.func_id);
        backend.reset(new virtio::PciModernBackend(pci, info));
    } else {
        zxlogf(SPEW, "virtio %02x:%02x.%1x using legacy PCI backend\n", info.bus_id, info.dev_id,
               info.func_id);
        backend.reset(new virtio::PciLegacyBackend(pci, info));
    }

    status = backend->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // Now that the backend for this device has been initialized we can
    // compose a device based on the PCI device id
    fbl::unique_ptr<virtio::Device> virtio_device = nullptr;
    switch (info.device_id) {
    case VIRTIO_DEV_TYPE_NETWORK:
    case VIRTIO_DEV_TYPE_T_NETWORK:
        virtio_device.reset(new virtio::EthernetDevice(bus_device, std::move(bti),
                                                       std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_BLOCK:
    case VIRTIO_DEV_TYPE_T_BLOCK:
        virtio_device.reset(new virtio::BlockDevice(bus_device, std::move(bti),
                                                    std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_CONSOLE:
    case VIRTIO_DEV_TYPE_T_CONSOLE:
        virtio_device.reset(new virtio::ConsoleDevice(bus_device, std::move(bti),
                                                      std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_GPU:
        virtio_device.reset(new virtio::GpuDevice(bus_device, std::move(bti), std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_ENTROPY:
    case VIRTIO_DEV_TYPE_T_ENTROPY:
        virtio_device.reset(new virtio::RngDevice(bus_device, std::move(bti), std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_INPUT:
        virtio_device.reset(new virtio::InputDevice(bus_device, std::move(bti),
                                                    std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_SOCKET:
        virtio_device.reset(new virtio::SocketDevice(bus_device, std::move(bti),
                                                     std::move(backend)));
        break;
    case VIRTIO_DEV_TYPE_SCSI:
    case VIRTIO_DEV_TYPE_T_SCSI_HOST:
        virtio_device.reset(new virtio::ScsiDevice(bus_device, std::move(bti),
                                                   std::move(backend)));
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = virtio_device->Init();
    if (status != ZX_OK) {
        return status;
    }

    // if we're here, we're successful so drop the unique ptr ref to the object and let it live on
    __UNUSED auto ptr = virtio_device.release();
    return ZX_OK;
}

static const zx_driver_ops_t virtio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = virtio_pci_bind,
    .create = nullptr,
    .release = nullptr,
};

ZIRCON_DRIVER_BEGIN(virtio, virtio_driver_ops, "zircon", "0.1", 16)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, VIRTIO_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_BLOCK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_CONSOLE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_ENTROPY),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_NETWORK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_SCSI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_BLOCK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_CONSOLE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_ENTROPY),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_NETWORK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_SCSI_HOST),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_GPU),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_INPUT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_SOCKET),
    BI_ABORT(),
ZIRCON_DRIVER_END(virtio)
