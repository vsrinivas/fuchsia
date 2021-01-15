// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zircon/errors.h>
#include <zircon/status.h>

#include "bus.h"

namespace pci {

zx_status_t Bus::LinkDevice(fbl::RefPtr<pci::Device> device) {
  fbl::AutoLock _(&devices_lock_);
  if (devices_.find(device->config()->bdf())) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  devices_.insert(device);
  return ZX_OK;
}

zx_status_t Bus::UnlinkDevice(pci::Device* device) {
  fbl::AutoLock _(&devices_lock_);
  ZX_DEBUG_ASSERT(device);
  if (devices_.find(device->config()->bdf())) {
    devices_.erase(*device);
    return ZX_OK;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t Bus::AllocateMsi(uint32_t count, zx::msi* msi) {
  fbl::AutoLock _(&devices_lock_);
  return pciroot().AllocateMsi(count, false, msi);
}

zx_status_t Bus::GetBti(const pci::Device* device, uint32_t index, zx::bti* bti) {
  if (!device) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AutoLock devices_lock(&devices_lock_);
  return pciroot().GetBti(device->packed_addr(), index, bti);
}

zx_status_t Bus::ConnectSysmem(zx::channel channel) {
  fbl::AutoLock _(&devices_lock_);
  return pciroot().ConnectSysmem(std::move(channel));
}

}  // namespace pci
