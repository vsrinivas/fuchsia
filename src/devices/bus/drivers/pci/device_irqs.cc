// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/stdcompat/bit.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>

#include "src/devices/bus/drivers/pci/capabilities/msi.h"
#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/device.h"

namespace pci {

zx::status<uint32_t> Device::QueryIrqMode(pci_irq_mode_t mode) {
  fbl::AutoLock dev_lock(&dev_lock_);
  switch (mode) {
    case PCI_IRQ_MODE_LEGACY:
    case PCI_IRQ_MODE_LEGACY_NOACK:
      if (cfg_->Read(Config::kInterruptLine) != 0) {
        return zx::ok(PCI_LEGACY_INT_COUNT);
      }
      break;
    case PCI_IRQ_MODE_MSI:
      if (caps_.msi) {
        return zx::ok(caps_.msi->vectors_avail());
      }
      break;
#ifdef ENABLE_MSIX
    case PCI_IRQ_MODE_MSI_X:
      if (caps_.msix) {
        return zx::ok(caps_.msix->table_size());
      }
      break;
#endif
    case PCI_IRQ_MODE_DISABLED:
    default:
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

pci_interrupt_modes_t Device::GetInterruptModes() {
  fbl::AutoLock dev_lock(&dev_lock_);
  pci_interrupt_modes_t modes{};

  if (cfg_->Read(Config::kInterruptLine) != 0) {
    modes.legacy = 1;
  }
  if (caps_.msi) {
    modes.msi = caps_.msi->vectors_avail();
  }
#ifdef ENABLE_MSIX
  if (caps_.msix) {
    modes.msix = caps_.msix->table_size();
  }
#endif

  return modes;
}

zx_status_t Device::SetIrqMode(pci_irq_mode_t mode, uint32_t irq_cnt) {
  if (mode >= PCI_IRQ_MODE_COUNT) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (mode != PCI_IRQ_MODE_DISABLED && irq_cnt == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock dev_lock(&dev_lock_);
  // Before enabling any given interrupt mode we need to ensure no existing
  // interrupts are configured. Disabling them can fail in cases downstream
  // drivers have mot freed outstanding interrupt objects allocated off of
  // an MSI object.
  if (zx_status_t st = DisableInterrupts(); st != ZX_OK) {
    return st;
  }

  // At this point interrupts have been disabled, so we're already successful
  // if that was the intent.
  if (mode == PCI_IRQ_MODE_DISABLED) {
    return ZX_OK;
  }

  switch (mode) {
    case PCI_IRQ_MODE_LEGACY:
      return EnableLegacy(/*needs_ack=*/true);
    case PCI_IRQ_MODE_LEGACY_NOACK:
      return EnableLegacy(/*needs_ack=*/false);
    case PCI_IRQ_MODE_MSI:
      if (caps_.msi) {
        return EnableMsi(irq_cnt);
      }
      break;
#ifdef ENABLE_MSIX
    case PCI_IRQ_MODE_MSI_X:
      if (caps_.msix) {
        return EnableMsix(irq_cnt);
      }
      break;
#endif
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::DisableInterrupts() {
  zx_status_t st = ZX_OK;
  switch (irqs_.mode) {
    case PCI_IRQ_MODE_DISABLED:
      return ZX_OK;
    case PCI_IRQ_MODE_LEGACY:
    case PCI_IRQ_MODE_LEGACY_NOACK:
      st = DisableLegacy();
      break;
    case PCI_IRQ_MODE_MSI:
      st = DisableMsi();
      break;
#ifdef ENABLE_MSIX
    case PCI_IRQ_MODE_MSI_X:
      st = DisableMsix();
      break;
#endif
  }

  if (st == ZX_OK) {
    zxlogf(DEBUG, "[%s] disabled IRQ mode %u", cfg_->addr(), irqs_.mode);
    irqs_.mode = PCI_IRQ_MODE_DISABLED;
    metrics_.irq_mode.Set(kInspectIrqModes[irqs_.mode]);
  }
  return st;
}

zx::status<zx::interrupt> Device::MapInterrupt(uint32_t which_irq) {
  fbl::AutoLock dev_lock(&dev_lock_);
  // MSI support is controlled through the capability held within the device's configuration space,
  // so the dispatcher needs access to the given device's config vmo. MSI-X needs access to the
  // table structure which is held in one of the device BARs, but a view is built ahead of time for
  // it when the MSI-X capability is initialized.
  if (irqs_.mode == PCI_IRQ_MODE_DISABLED) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  zx::interrupt interrupt = {};
  zx_status_t status = ZX_OK;
  switch (irqs_.mode) {
    case PCI_IRQ_MODE_LEGACY:
    case PCI_IRQ_MODE_LEGACY_NOACK: {
      if (which_irq != 0) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      status = irqs_.legacy.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupt);
      break;
    }
    case PCI_IRQ_MODE_MSI: {
      zx::status<fdf::MmioView> view_res = cfg_->get_view();
      if (!view_res.is_ok()) {
        return view_res.take_error();
      }

      status = zx::msi::create(irqs_.msi_allocation, /*options=*/0, which_irq, *view_res->get_vmo(),
                               view_res->get_offset() + caps_.msi->base(), &interrupt);
      break;
    }
#ifdef ENABLE_MSIX
    case PCI_IRQ_MODE_MSI_X: {
      auto& msix = caps_.msix;
      status = zx::msi::create(irqs_.msi_allocation, ZX_MSI_MODE_MSI_X, which_irq,
                               *msix->table_vmo(), msix->table_offset(), &interrupt);
      // Disable the function level masking now that at least one interrupt exists for the device.
      if (status == ZX_OK) {
        MsixControlReg ctrl = {.value = cfg_->Read(caps_.msix->ctrl())};
        ctrl.set_function_mask(0);
        cfg_->Write(caps_.msix->ctrl(), ctrl.value);
      }
      break;
    }
#endif
    default:
      return zx::error(ZX_ERR_BAD_STATE);
  }

  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(interrupt));
}

zx_status_t Device::SignalLegacyIrq(zx_time_t timestamp) const {
  metrics_.legacy.signal_count.Add(1);
  return irqs_.legacy.trigger(/*options=*/0, zx::time(timestamp));
}

zx_status_t Device::AckLegacyIrq() {
  if (irqs_.mode != PCI_IRQ_MODE_LEGACY) {
    return ZX_ERR_BAD_STATE;
  }

  EnableLegacyIrq();
  metrics_.legacy.ack_count.Add(1);
  return ZX_OK;
}

void Device::EnableLegacyIrq() {
  ModifyCmdLocked(/*clr_bits=*/PCI_CFG_COMMAND_INT_DISABLE, /*set_bits=*/0);
  irqs_.legacy_disabled = false;
  metrics_.legacy.disabled.Set(irqs_.legacy_disabled);
}

void Device::DisableLegacyIrq() {
  ModifyCmdLocked(/*clr_bits=*/0, /*set_bits=*/PCI_CFG_COMMAND_INT_DISABLE);
  irqs_.legacy_disabled = true;
  metrics_.legacy.disabled.Set(irqs_.legacy_disabled);
}

zx::status<std::pair<zx::msi, zx_info_msi_t>> Device::AllocateMsi(uint32_t irq_cnt) {
  zx::msi msi;
  zx_status_t st = bdi_->AllocateMsi(irq_cnt, &msi);
  if (st != ZX_OK) {
    return zx::error(st);
  }

  zx_info_msi_t msi_info;
  st = msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr);
  if (st != ZX_OK) {
    return zx::error(st);
  }
  ZX_DEBUG_ASSERT(msi_info.num_irq == irq_cnt);
  ZX_DEBUG_ASSERT(msi_info.interrupt_count == 0);

  metrics_.msi.allocated.Set(msi_info.num_irq);
  metrics_.msi.base_vector.Set(msi_info.base_irq_id);
  return zx::ok(std::make_pair(std::move(msi), msi_info));
}

zx_status_t Device::EnableLegacy(bool needs_ack) {
  irqs_.legacy_vector = cfg_->Read(Config::kInterruptLine);
  if (irqs_.legacy_vector == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = bdi_->AddToSharedIrqList(this, irqs_.legacy_vector);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] failed to add legacy irq to shared handler list %#x: %s", cfg_->addr(),
           irqs_.legacy_vector, zx_status_get_string(status));
    return status;
  }

  ModifyCmdLocked(/*clr_bits=*/PCIE_CFG_COMMAND_INT_DISABLE, /*set_bits=*/0);
  irqs_.mode = (needs_ack) ? PCI_IRQ_MODE_LEGACY : PCI_IRQ_MODE_LEGACY_NOACK;
  metrics_.irq_mode.Set(kInspectIrqModes[irqs_.mode]);
  return ZX_OK;
}

zx_status_t Device::EnableMsi(uint32_t irq_cnt) {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_DISABLED);
  ZX_DEBUG_ASSERT(!irqs_.msi_allocation);
  ZX_DEBUG_ASSERT(caps_.msi);

  if (!cpp20::has_single_bit(irq_cnt) || irq_cnt > caps_.msi->vectors_avail()) {
    zxlogf(DEBUG, "[%s] EnableMsi: bad irq count = %u, available = %u\n", cfg_->addr(), irq_cnt,
           caps_.msi->vectors_avail());
    return ZX_ERR_INVALID_ARGS;
  }

  // Bus mastering must be enabled to generate MSI messages.
  zx_status_t status = EnableBusMaster(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to enable bus mastering for MSI mode (%d)", cfg_->addr(), status);
    return status;
  }

  auto result = AllocateMsi(irq_cnt);
  if (result.is_ok()) {
    auto [alloc, info] = std::move(result.value());
    MsiControlReg ctrl = {.value = cfg_->Read(caps_.msi->ctrl())};
    cfg_->Write(caps_.msi->tgt_addr(), info.target_addr);
    cfg_->Write(caps_.msi->tgt_data(), info.target_data);
    if (ctrl.mm_capable()) {
      ctrl.set_mm_enable(MsiCapability::CountToMmc(irq_cnt));
    }
    ctrl.set_enable(1);
    cfg_->Write(caps_.msi->ctrl(), ctrl.value);

    irqs_.msi_allocation = std::move(alloc);
    irqs_.mode = PCI_IRQ_MODE_MSI;
    metrics_.irq_mode.Set(kInspectIrqModes[irqs_.mode]);
  }
  return result.status_value();
}

zx_status_t Device::EnableMsix(uint32_t irq_cnt) {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_DISABLED);
  ZX_DEBUG_ASSERT(!irqs_.msi_allocation);
  ZX_DEBUG_ASSERT(caps_.msix);

  // Bus mastering must be enabled to generate MSI-X messages.
  zx_status_t status = EnableBusMaster(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to enable bus mastering for MSI-X mode (%d)", cfg_->addr(), status);
    return status;
  }

  // MSI-X supports non-pow2 counts, but the MSI allocator still allocates in
  // pow2 based blocks.
  uint32_t irq_cnt_pow2 = cpp20::bit_ceil(irq_cnt);
  auto result = AllocateMsi(irq_cnt_pow2);
  if (result.is_ok()) {
    auto [alloc, info] = std::move(result.value());
    // Enable MSI-X, but mask off all functions until an interrupt is mapped.
    MsixControlReg ctrl = {.value = cfg_->Read(caps_.msix->ctrl())};
    ctrl.set_function_mask(1);
    ctrl.set_enable(1);
    cfg_->Write(caps_.msix->ctrl(), ctrl.value);

    irqs_.msi_allocation = std::move(alloc);
    irqs_.mode = PCI_IRQ_MODE_MSI_X;
    metrics_.irq_mode.Set(kInspectIrqModes[PCI_IRQ_MODE_MSI_X]);
  }
  return result.status_value();
}

zx_status_t Device::DisableLegacy() {
  zx_status_t status = bdi_->RemoveFromSharedIrqList(this, irqs_.legacy_vector);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] failed to remove legacy irq to shared handler list %#x: %s", cfg_->addr(),
           irqs_.legacy_vector, zx_status_get_string(status));
    return status;
  }

  ModifyCmdLocked(/*clr_bits=*/0, /*set_bits=*/PCIE_CFG_COMMAND_INT_DISABLE);
  irqs_.legacy_vector = 0;
  return ZX_OK;
}

// In general, if a device driver tries to disable an interrupt mode while
// holding handles to individual interrupts then it's considered a bad state.
// TODO(fxbug.dev/32978): Are there cases where the bus driver would want to hard disable
// IRQs even though the driver holds outstanding handles? In the event of a driver
// crash the handles will be released, but in a hard disable path they would still
// exist.
zx_status_t Device::VerifyAllMsisFreed() {
  if (!irqs_.msi_allocation) {
    return ZX_OK;
  }

  zx_info_msi_t info = {};
  zx_status_t st =
      irqs_.msi_allocation.get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr);
  if (st != ZX_OK) {
    return st;
  }

  if (info.interrupt_count != 0) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

void Device::DisableMsiCommon() {
  irqs_.msi_allocation.reset();
  metrics_.msi.allocated.Set(0);
  metrics_.msi.base_vector.Set(0);
}

zx_status_t Device::DisableMsi() {
  ZX_DEBUG_ASSERT(caps_.msi);
  if (zx_status_t st = VerifyAllMsisFreed(); st != ZX_OK) {
    return st;
  }

  MsiControlReg ctrl = {.value = cfg_->Read(caps_.msi->ctrl())};
  ctrl.set_enable(0);
  cfg_->Write(caps_.msi->ctrl(), ctrl.value);

  DisableMsiCommon();
  return ZX_OK;
}

zx_status_t Device::DisableMsix() {
  ZX_DEBUG_ASSERT(caps_.msix);
  if (zx_status_t st = VerifyAllMsisFreed(); st != ZX_OK) {
    return st;
  }

  MsixControlReg ctrl = {.value = cfg_->Read(caps_.msix->ctrl())};
  ctrl.set_function_mask(1);
  ctrl.set_enable(0);
  cfg_->Write(caps_.msix->ctrl(), ctrl.value);

  irqs_.msi_allocation.reset();
  return ZX_OK;
}

}  // namespace pci
