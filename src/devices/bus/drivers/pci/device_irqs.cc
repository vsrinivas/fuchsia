// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/mmio/mmio.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <ddk/protocol/pci.h>
#include <ddktl/protocol/pci.h>
#include <fbl/algorithm.h>

#include "capabilities/msi.h"
#include "common.h"
#include "device.h"

namespace pci {

zx_status_t Device::QueryIrqMode(pci_irq_mode_t mode, uint32_t* max_irqs) {
  ZX_DEBUG_ASSERT(max_irqs);
  fbl::AutoLock dev_lock(&dev_lock_);
  switch (mode) {
    case PCI_IRQ_MODE_LEGACY:
      return ZX_ERR_NOT_SUPPORTED;
    case PCI_IRQ_MODE_MSI:
      if (caps_.msi) {
        *max_irqs = caps_.msi->vectors_avail();
        return ZX_OK;
      }
      break;
    case PCI_IRQ_MODE_MSI_X:
      if (caps_.msix) {
        *max_irqs = caps_.msix->table_size();
        return ZX_OK;
      }
      break;
    case PCI_IRQ_MODE_DISABLED:
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_ERR_NOT_SUPPORTED;
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
    case PCI_IRQ_MODE_MSI:
      return EnableMsi(irq_cnt);
    case PCI_IRQ_MODE_MSI_X:
      return EnableMsix(irq_cnt);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::DisableInterrupts() {
  zxlogf(DEBUG, "[%s] disabling IRQ mode %u", cfg_->addr(), irqs_.mode);
  zx_status_t st = ZX_OK;
  switch (irqs_.mode) {
    case PCI_IRQ_MODE_DISABLED:
      zxlogf(DEBUG, "[%s] disabling interrupts when interrupts are already disabled", cfg_->addr());
      break;
    case PCI_IRQ_MODE_MSI:
      st = DisableMsi();
      break;
    case PCI_IRQ_MODE_MSI_X:
      st = DisableMsix();
      break;
  }

  if (st == ZX_OK) {
    irqs_.mode = PCI_IRQ_MODE_DISABLED;
  }
  return st;
}

zx::status<zx::interrupt> Device::MapInterrupt(uint32_t which_irq) {
  fbl::AutoLock dev_lock(&dev_lock_);
  // MSI support is controlled through the capability held within the device's configuration space,
  // so the dispatcher needs acess to the given device's config vmo. MSI-X needs access to the table
  // structure which is held in one of the device BARs, but a view is built ahead of time for it
  // when the MSI-X capability is initialized.
  if (irqs_.mode == PCI_IRQ_MODE_DISABLED) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  if (irqs_.mode == PCI_IRQ_MODE_LEGACY) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::interrupt interrupt = {};
  zx_status_t status = ZX_OK;
  switch (irqs_.mode) {
    case PCI_IRQ_MODE_MSI: {
      zx::status<ddk::MmioView> view_res = cfg_->get_view();
      if (!view_res.is_ok()) {
        return view_res.take_error();
      }

      status = zx::msi::create(irqs_.msi_allocation, /*options=*/0, which_irq, *view_res->get_vmo(),
                               view_res->get_offset() + caps_.msi->base(), &interrupt);
      break;
    }
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
    default:
      return zx::error(ZX_ERR_BAD_STATE);
  }

  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(interrupt));
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

  return zx::ok(std::make_pair(std::move(msi), msi_info));
}

zx_status_t Device::EnableMsi(uint32_t irq_cnt) {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_DISABLED);
  ZX_DEBUG_ASSERT(!irqs_.msi_allocation);
  ZX_DEBUG_ASSERT(caps_.msi);

  if (!fbl::is_pow2(irq_cnt) || irq_cnt > caps_.msi->vectors_avail()) {
    zxlogf(DEBUG, "[%s] EnableMsi: bad irq count = %u, available = %u\n", cfg_->addr(), irq_cnt,
           caps_.msi->vectors_avail());
    return ZX_ERR_INVALID_ARGS;
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
  }
  return result.status_value();
}

zx_status_t Device::EnableMsix(uint32_t irq_cnt) {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_DISABLED);
  ZX_DEBUG_ASSERT(!irqs_.msi_allocation);
  ZX_DEBUG_ASSERT(caps_.msix);

  // MSI-X supports non-pow2 counts, but the MSI allocator still allocates in
  // pow2 based blocks.
  uint32_t irq_cnt_pow2 = fbl::roundup_pow2(irq_cnt);
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
  }
  return result.status_value();
}

// In general, if a device driver tries to disable an interrupt mode while
// holding handles to individual interrupts then it's considered a bad state.
// TODO(32978): Are there cases where the bus driver would want to hard disable
// IRQs even though the driver holds outstanding handles? In the event of a driver
// crash the handles will be released, but in a hard disable path they would still
// exist.
zx_status_t Device::VerifyAllMsisFreed() {
  ZX_DEBUG_ASSERT(irqs_.msi_allocation);
  zx_info_msi_t info;
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

zx_status_t Device::DisableMsi() {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_MSI);
  ZX_DEBUG_ASSERT(caps_.msi);
  if (zx_status_t st = VerifyAllMsisFreed(); st != ZX_OK) {
    return st;
  }

  MsiControlReg ctrl = {.value = cfg_->Read(caps_.msi->ctrl())};
  ctrl.set_enable(0);
  cfg_->Write(caps_.msi->ctrl(), ctrl.value);

  irqs_.msi_allocation.reset();
  irqs_.mode = PCI_IRQ_MODE_DISABLED;
  return ZX_OK;
}

zx_status_t Device::DisableMsix() {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_MSI_X);
  ZX_DEBUG_ASSERT(caps_.msix);
  if (zx_status_t st = VerifyAllMsisFreed(); st != ZX_OK) {
    return st;
  }

  MsixControlReg ctrl = {.value = cfg_->Read(caps_.msix->ctrl())};
  ctrl.set_function_mask(1);
  ctrl.set_enable(0);
  cfg_->Write(caps_.msix->ctrl(), ctrl.value);

  irqs_.msi_allocation.reset();
  irqs_.mode = PCI_IRQ_MODE_DISABLED;
  return ZX_OK;
}

}  // namespace pci
