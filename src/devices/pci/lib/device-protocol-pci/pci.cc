// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pci/cpp/wire_types.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>

zx_status_t pci_configure_interrupt_mode(const pci_protocol_t* pci, uint32_t requested_irq_count,
                                         pci_interrupt_mode_t* out_mode) {
  // NOTE: Any changes to this method should likely also be reflected in the C++
  // version, Pci::ConfigureInterruptMode. These two implementations are
  // temporarily coexisting while we migrate PCI from Banjo to FIDL. Eventually
  // the C version will go away.
  //
  // TODO(fxbug.dev/99914): Remove this function once PCI over Banjo is removed.
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

namespace fpci = fuchsia_hardware_pci;

zx_status_t Pci::GetDeviceInfo(fpci::wire::DeviceInfo* out_info) const {
  auto result = client_->GetDeviceInfo();
  if (!result.ok()) {
    return result.status();
  }
  *out_info = result.value().info;
  return ZX_OK;
}

zx_status_t Pci::GetBar(fidl::AnyArena& arena, uint32_t bar_id, fpci::wire::Bar* out_result) const {
  auto result = client_.buffer(arena)->GetBar(bar_id);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_result = std::move(result->value()->result);
  if (out_result->result.is_io()) {
    zx_status_t status = zx_ioports_request(out_result->result.io().resource.get(),
                                            static_cast<uint16_t>(out_result->result.io().address),
                                            static_cast<uint32_t>(out_result->size));
    return status;
  } else {
    return ZX_OK;
  }
}

zx_status_t Pci::SetBusMastering(bool enabled) const {
  auto result = client_->SetBusMastering(enabled);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::ResetDevice() const {
  auto result = client_->ResetDevice();
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::AckInterrupt() const {
  auto result = client_->AckInterrupt();
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::MapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt) const {
  auto result = client_->MapInterrupt(which_irq);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_interrupt = std::move(result->value()->interrupt);
  return ZX_OK;
}

void Pci::GetInterruptModes(fpci::wire::InterruptModes* out_modes) const {
  auto result = client_->GetInterruptModes();
  if (!result.ok()) {
    return;
  }
  *out_modes = result.value().modes;
}

zx_status_t Pci::SetInterruptMode(fpci::InterruptMode mode, uint32_t requested_irq_count) const {
  auto result = client_->SetInterruptMode(mode, requested_irq_count);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::ReadConfig8(uint16_t offset, uint8_t* out_value) const {
  auto result = client_->ReadConfig8(offset);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_value = result->value()->value;
  return ZX_OK;
}

zx_status_t Pci::ReadConfig16(uint16_t offset, uint16_t* out_value) const {
  auto result = client_->ReadConfig16(offset);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_value = result->value()->value;
  return ZX_OK;
}

zx_status_t Pci::ReadConfig32(uint16_t offset, uint32_t* out_value) const {
  auto result = client_->ReadConfig32(offset);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_value = result->value()->value;
  return ZX_OK;
}

zx_status_t Pci::WriteConfig8(uint16_t offset, uint8_t value) const {
  auto result = client_->WriteConfig8(offset, value);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::WriteConfig16(uint16_t offset, uint16_t value) const {
  auto result = client_->WriteConfig16(offset, value);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::WriteConfig32(uint16_t offset, uint32_t value) const {
  auto result = client_->WriteConfig32(offset, value);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Pci::GetFirstCapability(fpci::CapabilityId id, uint8_t* out_offset) const {
  auto result = client_->GetCapabilities(id);
  if (!result.ok()) {
    return result.status();
  }

  if (result.value().offsets.count() == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  *out_offset = result.value().offsets[0];
  return ZX_OK;
}

zx_status_t Pci::GetNextCapability(fpci::CapabilityId id, uint8_t start_offset,
                                   uint8_t* out_offset) const {
  auto result = client_->GetCapabilities(id);
  if (!result.ok()) {
    return result.status();
  }
  fidl::VectorView<uint8_t> offsets = result.value().offsets;
  for (uint64_t i = 0; i < offsets.count() - 1; i++) {
    if (offsets[i] == start_offset) {
      *out_offset = offsets[i + 1];
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t Pci::GetFirstExtendedCapability(fpci::ExtendedCapabilityId id,
                                            uint16_t* out_offset) const {
  auto result = client_->GetExtendedCapabilities(id);
  if (!result.ok()) {
    return result.status();
  }

  if (result.value().offsets.count() == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  *out_offset = result.value().offsets[0];
  return ZX_OK;
}

zx_status_t Pci::GetNextExtendedCapability(fpci::ExtendedCapabilityId id, uint16_t start_offset,
                                           uint16_t* out_offset) const {
  auto result = client_->GetExtendedCapabilities(id);
  if (!result.ok()) {
    return result.status();
  }
  auto offsets = result.value().offsets;
  for (uint64_t i = 0; i < offsets.count() - 1; i++) {
    if (offsets[i] == start_offset) {
      *out_offset = offsets[i + 1];
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t Pci::GetBti(uint32_t index, zx::bti* out_bti) const {
  auto result = client_->GetBti(index);
  if (!result.ok()) {
    return result.status();
  }
  if (result->is_error()) {
    return result->error_value();
  }
  *out_bti = std::move(result->value()->bti);
  return ZX_OK;
}

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy,
                         std::optional<fdf::MmioBuffer>* mmio) const {
  zx::vmo vmo;
  zx_status_t status = MapMmioInternal(index, cache_policy, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return status;
  }

  return fdf::MmioBuffer::Create(0, vmo_size, std::move(vmo), cache_policy, mmio);
}

zx_status_t Pci::MapMmio(uint32_t index, uint32_t cache_policy, mmio_buffer_t* mmio) const {
  zx::vmo vmo;
  zx_status_t status = MapMmioInternal(index, cache_policy, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return status;
  }

  return mmio_buffer_init(mmio, 0, vmo_size, vmo.release(), cache_policy);
}

zx_status_t Pci::MapMmioInternal(uint32_t index, uint32_t cache_policy, zx::vmo* out_vmo) const {
  fidl::Arena arena;
  fpci::wire::Bar bar;
  zx_status_t status = GetBar(arena, index, &bar);
  if (status != ZX_OK) {
    return status;
  }

  // TODO(cja): PIO may be mappable on non-x86 architectures
  if (bar.result.is_io()) {
    return ZX_ERR_WRONG_TYPE;
  }

  *out_vmo = std::move(bar.result.vmo());
  return ZX_OK;
}

zx_status_t Pci::ConfigureInterruptMode(uint32_t requested_irq_count,
                                        fpci::InterruptMode* out_mode) const {
  // NOTE: Any changes to this method should likely also be reflected in the C
  // version, pci_configure_interrupt_mode. These two implementations are
  // temporarily coexisting while we migrate PCI from Banjo to FIDL. Eventually
  // the C version will go away.
  //
  // TODO(fxbug.dev/99914): Remove this notice once PCI over Banjo is removed.
  if (requested_irq_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  fpci::wire::InterruptModes modes;
  GetInterruptModes(&modes);
  std::pair<fpci::InterruptMode, uint32_t> pairs[] = {
      {fpci::InterruptMode::kMsiX, modes.msix_count},
      {fpci::InterruptMode::kMsi, modes.msi_count},
      {fpci::InterruptMode::kLegacy, modes.has_legacy}};
  for (auto& [mode, irq_cnt] : pairs) {
    if (irq_cnt >= requested_irq_count) {
      zx_status_t status = SetInterruptMode(mode, requested_irq_count);
      if (status == ZX_OK) {
        if (out_mode) {
          *out_mode = fpci::InterruptMode{mode};
        }
        return status;
      }
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace ddk
