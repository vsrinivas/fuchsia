// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>

zx_status_t pci_configure_interrupt_mode(const pci_protocol_t* pci, uint32_t requested_irq_count,
                                         pci_interrupt_mode_t* out_mode) {
  if (requested_irq_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  pci_interrupt_modes modes{};
  pci_get_interrupt_modes(pci, &modes);
  std::pair<pci_interrupt_mode_t, uint32_t> pairs[] = {
      {PCI_INTERRUPT_MODE_MSI_X, modes.msix_count},
      {PCI_INTERRUPT_MODE_MSI, modes.msi_count},
      {PCI_INTERRUPT_MODE_LEGACY, modes.has_legacy}};
  for (auto& [mode, irq_cnt] : pairs) {
    if (irq_cnt >= requested_irq_count) {
      zx_status_t status = pci_set_interrupt_mode(pci, mode, requested_irq_count);
      if (status == ZX_OK) {
        if (out_mode) {
          *out_mode = mode;
        }
        return status;
      }
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
  if (bar.type == PCI_BAR_TYPE_IO) {
    return ZX_ERR_WRONG_TYPE;
  }

  size_t vmo_size;
  st = zx_vmo_get_size(bar.result.vmo, &vmo_size);
  if (st != ZX_OK) {
    zx_handle_close(bar.result.vmo);
    return st;
  }

  return mmio_buffer_init(buffer, 0, vmo_size, bar.result.vmo, cache_policy);
}

namespace ddk {

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy,
                         std::optional<fdf::MmioBuffer>* mmio) {
  pci_bar_t bar;
  zx_status_t status = GetBar(index, &bar);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(cja): PIO may be mappable on non-x86 architectures
  if (bar.type == PCI_BAR_TYPE_IO) {
    return ZX_ERR_WRONG_TYPE;
  }

  zx::vmo vmo(bar.result.vmo);

  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return status;
  }

  return fdf::MmioBuffer::Create(0, vmo_size, std::move(vmo), cache_policy, mmio);
}

zx_status_t Pci::ConfigureInterruptMode(uint32_t requested_irq_count,
                                        pci_interrupt_mode_t* out_mode) {
  pci_protocol_t proto{};
  GetProto(&proto);
  return pci_configure_interrupt_mode(&proto, requested_irq_count, out_mode);
}

}  // namespace ddk
