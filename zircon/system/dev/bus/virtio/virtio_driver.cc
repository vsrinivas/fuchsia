// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <fbl/alloc_checker.h>

#include "backends/pci.h"
#include "console.h"
#include "device.h"
#include "driver_utils.h"
#include "ethernet.h"
#include "gpu.h"
#include "input.h"
#include "rng.h"
#include "scsi.h"
#include "socket.h"

static bool gpu_disabled() {
  const char* flag = getenv("driver.virtio-gpu.disable");
  return (flag != nullptr && (!strcmp(flag, "1") || !strcmp(flag, "true") || !strcmp(flag, "on")));
}

static zx_status_t virtio_pci_bind(void* ctx, zx_device_t* bus_device) {
  zx_status_t status;
  pci_protocol_t pci;

  // Grab the pci device and configuration to pass to the backend.
  if (device_get_protocol(bus_device, ZX_PROTOCOL_PCI, &pci)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_pcie_device_info_t info;
  status = pci_get_device_info(&pci, &info);
  if (status != ZX_OK) {
    return status;
  }

  // Compose a device based on the PCI device id.
  switch (info.device_id) {
    case VIRTIO_DEV_TYPE_NETWORK:
    case VIRTIO_DEV_TYPE_T_NETWORK:
      return CreateAndBind<virtio::EthernetDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_CONSOLE:
    case VIRTIO_DEV_TYPE_T_CONSOLE:
      return CreateAndBind<virtio::ConsoleDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_GPU:
      if (gpu_disabled()) {
        zxlogf(INFO, "driver.virtio-gpu.disabled=1, not binding to the GPU\n");
        return ZX_ERR_NOT_FOUND;
      }
      return CreateAndBind<virtio::GpuDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_ENTROPY:
    case VIRTIO_DEV_TYPE_T_ENTROPY:
      return CreateAndBind<virtio::RngDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_INPUT:
      return CreateAndBind<virtio::InputDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_SOCKET:
      return CreateAndBind<virtio::SocketDevice>(ctx, bus_device);
    case VIRTIO_DEV_TYPE_SCSI:
    case VIRTIO_DEV_TYPE_T_SCSI_HOST:
      return CreateAndBind<virtio::ScsiDevice>(ctx, bus_device);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

static const zx_driver_ops_t virtio_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio_pci_bind;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(virtio, virtio_driver_ops, "zircon", "0.1", 16)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, VIRTIO_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_CONSOLE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_ENTROPY),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_NETWORK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_SCSI),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_CONSOLE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_ENTROPY),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_NETWORK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_SCSI_HOST),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_GPU),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_INPUT),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_SOCKET), BI_ABORT(), ZIRCON_DRIVER_END(virtio)
