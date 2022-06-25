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

zx_status_t Pci::GetDeviceInfo(pci_device_info_t* out_info) const {
  return client_.GetDeviceInfo(out_info);
}

zx_status_t Pci::GetBar(uint32_t bar_id, pci_bar_t* out_result) const {
  return client_.GetBar(bar_id, out_result);
}

zx_status_t Pci::SetBusMastering(bool enabled) const { return client_.SetBusMastering(enabled); }

zx_status_t Pci::ResetDevice() const { return client_.ResetDevice(); }

zx_status_t Pci::AckInterrupt() const { return client_.AckInterrupt(); }

zx_status_t Pci::MapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt) const {
  return client_.MapInterrupt(which_irq, out_interrupt);
}

void Pci::GetInterruptModes(pci_interrupt_modes_t* out_modes) const {
  return client_.GetInterruptModes(out_modes);
}

zx_status_t Pci::SetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count) const {
  return client_.SetInterruptMode(mode, requested_irq_count);
}

zx_status_t Pci::ReadConfig8(uint16_t offset, uint8_t* out_value) const {
  return client_.ReadConfig8(offset, out_value);
}

zx_status_t Pci::ReadConfig16(uint16_t offset, uint16_t* out_value) const {
  return client_.ReadConfig16(offset, out_value);
}
zx_status_t Pci::ReadConfig32(uint16_t offset, uint32_t* out_value) const {
  return client_.ReadConfig32(offset, out_value);
}

zx_status_t Pci::WriteConfig8(uint16_t offset, uint8_t value) const {
  return client_.WriteConfig8(offset, value);
}

zx_status_t Pci::WriteConfig16(uint16_t offset, uint16_t value) const {
  return client_.WriteConfig16(offset, value);
}

zx_status_t Pci::WriteConfig32(uint16_t offset, uint32_t value) const {
  return client_.WriteConfig32(offset, value);
}

zx_status_t Pci::GetFirstCapability(pci_capability_id_t id, uint8_t* out_offset) const {
  return client_.GetFirstCapability(id, out_offset);
}

zx_status_t Pci::GetNextCapability(pci_capability_id_t id, uint8_t start_offset,
                                   uint8_t* out_offset) const {
  return client_.GetNextCapability(id, start_offset, out_offset);
}

zx_status_t Pci::GetFirstExtendedCapability(pci_extended_capability_id_t id,
                                            uint16_t* out_offset) const {
  return client_.GetFirstExtendedCapability(id, out_offset);
}

zx_status_t Pci::GetNextExtendedCapability(pci_extended_capability_id_t id, uint16_t start_offset,
                                           uint16_t* out_offset) const {
  return client_.GetNextExtendedCapability(id, start_offset, out_offset);
}

zx_status_t Pci::GetBti(uint32_t index, zx::bti* out_bti) const {
  return client_.GetBti(index, out_bti);
}

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy,
                         std::optional<fdf::MmioBuffer>* mmio) const {
  pci_bar_t bar;
  zx_status_t status = client_.GetBar(index, &bar);
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
                                        pci_interrupt_mode_t* out_mode) const {
  pci_protocol_t proto{};
  client_.GetProto(&proto);
  return pci_configure_interrupt_mode(&proto, requested_irq_count, out_mode);
}

}  // namespace ddk
