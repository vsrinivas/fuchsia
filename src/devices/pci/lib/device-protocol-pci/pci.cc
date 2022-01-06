// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>

zx_status_t pci_configure_irq_mode(const pci_protocol_t* pci, uint32_t requested_irq_count,
                                   pci_irq_mode_t* out_mode) {
  pci_irq_mode_t modes[] = {PCI_IRQ_MODE_MSI_X, PCI_IRQ_MODE_MSI, PCI_IRQ_MODE_LEGACY};
  for (pci_irq_mode_t mode : modes) {
    uint32_t irq_cnt = 0;
    zx_status_t query_status = pci->ops->query_irq_mode(pci->ctx, mode, &irq_cnt);
    if (query_status == ZX_OK && irq_cnt >= requested_irq_count) {
      zx_status_t set_status = pci->ops->set_irq_mode(pci->ctx, mode, requested_irq_count);
      if (set_status == ZX_OK && out_mode) {
        *out_mode = mode;
      }
      return set_status;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t pci_map_bar_buffer(const pci_protocol_t* pci, uint32_t bar_id, uint32_t cache_policy,
                               mmio_buffer_t* buffer) {
  pci_bar_t bar;
  zx_status_t st = pci->ops->get_bar(pci->ctx, bar_id, &bar);
  if (st != ZX_OK) {
    return st;
  }
  // TODO(cja): PIO may be mappable on non-x86 architectures
  if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_WRONG_TYPE;
  }

  size_t vmo_size;
  st = zx_vmo_get_size(bar.handle, &vmo_size);
  if (st != ZX_OK) {
    zx_handle_close(bar.handle);
    return st;
  }

  return mmio_buffer_init(buffer, 0, vmo_size, bar.handle, cache_policy);
}

namespace ddk {

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy, std::optional<MmioBuffer>* mmio) {
  pci_bar_t bar;
  zx_status_t status = GetBar(index, &bar);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(cja): PIO may be mappable on non-x86 architectures
  if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_WRONG_TYPE;
  }

  zx::vmo vmo(bar.handle);

  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return status;
  }

  return MmioBuffer::Create(0, vmo_size, std::move(vmo), cache_policy, mmio);
}

zx_status_t Pci::ConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* out_mode) {
  pci_protocol_t proto{};
  GetProto(&proto);
  return pci_configure_irq_mode(&proto, requested_irq_count, out_mode);
}

}  // namespace ddk
