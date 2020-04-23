// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_utils.h"

#include <lib/fit/result.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

fit::result<std::pair<zx::bti, std::unique_ptr<virtio::Backend>>, zx_status_t> GetBtiAndBackend(
    zx_device_t* bus_device) {
  zx_status_t status;
  pci_protocol_t pci;

  // grab the pci device and configuration to pass to the backend
  if (device_get_protocol(bus_device, ZX_PROTOCOL_PCI, &pci)) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  zx_pcie_device_info_t info;
  status = pci_get_device_info(&pci, &info);
  if (status != ZX_OK) {
    return fit::error(status);
  }

  zx::bti bti;
  status = pci_get_bti(&pci, 0, bti.reset_and_get_address());
  if (status != ZX_OK) {
    return fit::error(status);
  }

  // Due to the similarity between Virtio 0.9.5 legacy devices and Virtio 1.0
  // transitional devices we need to check whether modern capabilities exist.
  // If no vendor capabilities are found then we will default to the legacy
  // interface.
  std::unique_ptr<virtio::Backend> backend = nullptr;
  uint8_t offset;
  if (pci_get_first_capability(&pci, PCI_CAP_ID_VENDOR, &offset) == ZX_OK) {
    zxlogf(SPEW, "virtio %02x:%02x.%1x using modern PCI backend", info.bus_id, info.dev_id,
           info.func_id);
    backend.reset(new virtio::PciModernBackend(pci, info));
  } else {
    zxlogf(SPEW, "virtio %02x:%02x.%1x using legacy PCI backend", info.bus_id, info.dev_id,
           info.func_id);
    backend.reset(new virtio::PciLegacyBackend(pci, info));
  }

  status = backend->Bind();
  if (status != ZX_OK) {
    return fit::error(status);
  }

  return fit::ok(std::make_pair(std::move(bti), std::move(backend)));
}
