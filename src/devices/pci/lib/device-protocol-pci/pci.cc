// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>

namespace ddk {

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy, std::optional<MmioBuffer>* mmio) {
  pci_bar_t bar;
  zx_status_t status = GetBar(index, &bar);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(cja): PIO may be mappable on non-x86 architectures
  if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.u.handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_WRONG_TYPE;
  }

  zx::vmo vmo(bar.u.handle);

  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return status;
  }

  return MmioBuffer::Create(0, vmo_size, std::move(vmo), cache_policy, mmio);
}

}  // namespace ddk
