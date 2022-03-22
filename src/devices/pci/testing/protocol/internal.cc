// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/pci/testing/protocol/internal.h"

#include <lib/stdcompat/bit.h>

namespace pci {

__EXPORT FakePciProtocolInternal::FakePciProtocolInternal() {
  // Reserve space ahead of time to ensure the vectors do not re-allocate so
  // references provided to callers stay valid.
  msi_interrupts_.reserve(MSI_MAX_VECTORS);
  msix_interrupts_.reserve(MSIX_MAX_VECTORS);
  reset();
}

zx_status_t FakePciProtocolInternal::FakePciProtocolInternal::PciGetBar(uint32_t bar_id,
                                                                        pci_bar_t* out_res) {
  if (!out_res) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (bar_id >= PCI_DEVICE_BAR_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto& bar = bars_[bar_id];
  if (!bar.vmo.has_value()) {
    return ZX_ERR_NOT_FOUND;
  }

  out_res->id = bar_id;
  out_res->size = bar.size;
  out_res->type = bar.type;
  out_res->address = 0;  // PIO bars use the address, MMIO uses the VMO.
  out_res->handle = bar.vmo->get();
  return ZX_OK;
}

zx_status_t FakePciProtocolInternal::PciAckInterrupt() {
  return (irq_mode_ == PCI_IRQ_MODE_LEGACY) ? ZX_OK : ZX_ERR_BAD_STATE;
}

zx_status_t FakePciProtocolInternal::PciMapInterrupt(uint32_t which_irq,
                                                     zx::interrupt* out_handle) {
  if (!out_handle) {
    return ZX_ERR_INVALID_ARGS;
  }

  switch (irq_mode_) {
    case PCI_IRQ_MODE_LEGACY:
    case PCI_IRQ_MODE_LEGACY_NOACK:
      if (which_irq > 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      return legacy_interrupt_->duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
    case PCI_IRQ_MODE_MSI:
      if (which_irq >= msi_interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return msi_interrupts_[which_irq].duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
    case PCI_IRQ_MODE_MSI_X:
      if (which_irq >= msix_interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return msix_interrupts_[which_irq].duplicate(ZX_RIGHT_SAME_RIGHTS, out_handle);
  }

  return ZX_ERR_BAD_STATE;
}

void FakePciProtocolInternal::PciGetInterruptModes(pci_interrupt_modes* out_modes) {
  pci_interrupt_modes_t modes{};
  if (legacy_interrupt_) {
    modes.legacy = 1;
  }
  if (!msi_interrupts_.empty()) {
    // MSI interrupts are only supported in powers of 2.
    modes.msi = static_cast<uint32_t>((msi_interrupts_.size() <= 1)
                                          ? msi_interrupts_.size()
                                          : fbl::round_down(msi_interrupts_.size(), 2u));
  }
  if (!msix_interrupts_.empty()) {
    modes.msix = static_cast<uint32_t>(msix_interrupts_.size());
  }
  *out_modes = modes;
}

zx_status_t FakePciProtocolInternal::PciSetInterruptMode(pci_irq_mode_t mode,
                                                         uint32_t requested_irq_count) {
  if (!AllMappedInterruptsFreed()) {
    return ZX_ERR_BAD_STATE;
  }

  switch (mode) {
    case PCI_IRQ_MODE_LEGACY:
    case PCI_IRQ_MODE_LEGACY_NOACK:
      if (requested_irq_count > 1) {
        return ZX_ERR_INVALID_ARGS;
      }

      if (legacy_interrupt_) {
        irq_mode_ = mode;
        irq_cnt_ = 1;
      }
      return ZX_OK;
    case PCI_IRQ_MODE_MSI:
      if (msi_interrupts_.empty()) {
        break;
      }
      if (!cpp20::has_single_bit(requested_irq_count) || requested_irq_count > MSI_MAX_VECTORS) {
        return ZX_ERR_INVALID_ARGS;
      }
      if (msi_interrupts_.size() < requested_irq_count) {
        return ZX_ERR_INVALID_ARGS;
      }
      irq_mode_ = PCI_IRQ_MODE_MSI;
      irq_cnt_ = requested_irq_count;
      return ZX_OK;
    case PCI_IRQ_MODE_MSI_X:
      if (msix_interrupts_.empty()) {
        break;
      }
      if (requested_irq_count > MSIX_MAX_VECTORS) {
        return ZX_ERR_INVALID_ARGS;
      }

      if (msix_interrupts_.size() < requested_irq_count) {
        return ZX_ERR_INVALID_ARGS;
      }
      irq_mode_ = PCI_IRQ_MODE_MSI_X;
      irq_cnt_ = requested_irq_count;
      return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FakePciProtocolInternal::PciEnableBusMaster(bool enable) {
  bus_master_en_ = enable;
  return ZX_OK;
}

zx_status_t FakePciProtocolInternal::PciResetDevice() {
  reset_cnt_++;
  return ZX_OK;
}

zx_status_t FakePciProtocolInternal::PciGetDeviceInfo(pcie_device_info_t* out_info) {
  ZX_ASSERT(out_info);
  *out_info = info_;
  return ZX_OK;
}

zx_status_t FakePciProtocolInternal::PciGetFirstCapability(uint8_t id, uint8_t* out_offset) {
  return CommonCapabilitySearch(id, std::nullopt, out_offset);
}

zx_status_t FakePciProtocolInternal::PciGetNextCapability(uint8_t id, uint8_t offset,
                                                          uint8_t* out_offset) {
  return CommonCapabilitySearch(id, offset, out_offset);
}

zx_status_t FakePciProtocolInternal::PciGetFirstExtendedCapability(uint16_t id,
                                                                   uint16_t* out_offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FakePciProtocolInternal::PciGetNextExtendedCapability(uint16_t id, uint16_t offset,
                                                                  uint16_t* out_offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FakePciProtocolInternal::PciGetBti(uint32_t index, zx::bti* out_bti) {
  if (!out_bti) {
    return ZX_ERR_INVALID_ARGS;
  }
  return bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_bti);
}

__EXPORT void FakePciProtocolInternal::AddCapabilityInternal(uint8_t capability_id,
                                                             uint8_t position, uint8_t size) {
  ZX_ASSERT_MSG(
      capability_id > 0 && capability_id <= PCI_CAP_ID_FLATTENING_PORTAL_BRIDGE,
      "FakePciProtocol Error: capability_id must be non-zero and <= %#x (capability_id = %#x).",
      PCI_CAP_ID_FLATTENING_PORTAL_BRIDGE, capability_id);
  ZX_ASSERT_MSG(position >= PCI_CFG_HEADER_SIZE && position + size < PCI_BASE_CONFIG_SIZE,
                "FakePciProtocolError: capability must fit the range [%#x, %#x] (capability = "
                "[%#x, %#x]).",
                PCI_CFG_HEADER_SIZE, PCI_BASE_CONFIG_SIZE - 1, position, position + size - 1);

  // We need to update the next pointer of the previous capability, or the
  // original header capabilities pointer if this is the first.
  uint8_t next_ptr = PCI_CFG_CAPABILITIES_PTR;
  if (!capabilities().empty()) {
    for (auto& cap : capabilities()) {
      ZX_ASSERT_MSG(!(position <= cap.position && position + size > cap.position) &&
                        !(position >= cap.position && position < cap.position + cap.size),
                    "FakePciProtocol Error: New capability overlaps with a previous capability "
                    "[%#x, %#x] (new capability id = %#x @ [%#x, %#x]).",
                    cap.position, cap.position + cap.size - 1, capability_id, position,
                    position + size - 1);
    }
    next_ptr = capabilities()[capabilities().size() - 1].position + 1;
  }

  config().write(&capability_id, position, sizeof(capability_id));
  config().write(&position, next_ptr, sizeof(position));
  capabilities().push_back({.id = capability_id, .position = position, .size = size});
  // Not fast, but not as error prone as doing it by hand on insertion with
  // capability cyles being a possibility.
  std::sort(capabilities().begin(), capabilities().end());
}

__EXPORT zx::interrupt& FakePciProtocolInternal::AddInterrupt(pci_irq_mode_t mode) {
  ZX_ASSERT_MSG(!(mode == PCI_IRQ_MODE_LEGACY && legacy_interrupt_),
                "FakePciProtocol Error: Legacy interrupt mode only supports 1 interrupt.");
  ZX_ASSERT_MSG(!(mode == PCI_IRQ_MODE_MSI && msi_interrupts_.size() == MSI_MAX_VECTORS),
                "FakePciProtocol Error: MSI interrupt mode only supports up to %u interrupts.",
                MSI_MAX_VECTORS);

  zx::interrupt interrupt{};
  zx_status_t status = zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), 0,
                                             ZX_INTERRUPT_VIRTUAL, &interrupt);
  ZX_ASSERT_MSG(status == ZX_OK, kFakePciInternalError);

  switch (mode) {
    case PCI_IRQ_MODE_LEGACY:
      legacy_interrupt_ = std::move(interrupt);
      return *legacy_interrupt_;
    case PCI_IRQ_MODE_MSI:
      msi_interrupts_.insert(msi_interrupts_.end(), std::move(interrupt));
      msi_count_++;
      return msi_interrupts_[msi_count_ - 1];
    case PCI_IRQ_MODE_MSI_X:
      msix_interrupts_.insert(msix_interrupts_.end(), std::move(interrupt));
      msix_count_++;
      return msix_interrupts_[msix_count_ - 1];
  }

  ZX_PANIC("%s", kFakePciInternalError);
}

__EXPORT pcie_device_info_t
FakePciProtocolInternal::SetDeviceInfoInternal(pcie_device_info_t new_info) {
  config().write(&new_info.vendor_id, PCI_CFG_VENDOR_ID, sizeof(info().vendor_id));
  config().write(&new_info.device_id, PCI_CFG_DEVICE_ID, sizeof(info().device_id));
  config().write(&new_info.revision_id, PCI_CFG_REVISION_ID, sizeof(info().revision_id));
  config().write(&new_info.base_class, PCI_CFG_CLASS_CODE_BASE, sizeof(info().base_class));
  config().write(&new_info.sub_class, PCI_CFG_CLASS_CODE_SUB, sizeof(info().sub_class));
  config().write(&new_info.program_interface, PCI_CFG_CLASS_CODE_INTR,
                 sizeof(info().program_interface));
  info_ = new_info;

  return new_info;
}

__EXPORT void FakePciProtocolInternal::reset() {
  legacy_interrupt_ = std::nullopt;
  msi_interrupts_.clear();
  msi_count_ = 0;
  msix_interrupts_.clear();
  msix_count_ = 0;
  irq_mode_ = PCI_IRQ_MODE_DISABLED;

  bars_ = {};
  capabilities_.clear();

  bus_master_en_ = std::nullopt;
  reset_cnt_ = 0;
  info() = {};

  zx_status_t status = zx::vmo::create(PCI_BASE_CONFIG_SIZE, /*options=*/0, &config());
  ZX_ASSERT(status == ZX_OK);
  status = fake_bti_create(bti_.reset_and_get_address());
  ZX_ASSERT(status == ZX_OK);
}

__EXPORT bool FakePciProtocolInternal::AllMappedInterruptsFreed() {
  zx_info_handle_count_t info;
  for (auto& interrupts : {&msix_interrupts_, &msi_interrupts_}) {
    for (auto& interrupt : *interrupts) {
      zx_status_t status =
          interrupt.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
      ZX_ASSERT_MSG(status == ZX_OK, "%s status %d", kFakePciInternalError, status);

      if (info.handle_count > 1) {
        return false;
      }
    }
  }
  return true;
}

__EXPORT zx_status_t FakePciProtocolInternal::CommonCapabilitySearch(uint8_t id,
                                                                     std::optional<uint8_t> offset,
                                                                     uint8_t* out_offset) {
  if (!out_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (auto& cap : capabilities_) {
    // Skip until we've caught up to last one found if one was provided.
    if (offset && cap.position <= offset) {
      continue;
    }

    if (cap.id == id) {
      *out_offset = cap.position;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

}  // namespace pci
