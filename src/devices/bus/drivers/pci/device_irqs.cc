// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zircon/assert.h>

#include <ddk/protocol/pci.h>
#include <ddktl/protocol/pci.h>
#include <fbl/algorithm.h>

#include "capabilities/msi.h"
#include "common.h"
#include "device.h"

namespace pci {

zx_status_t Device::QueryIrqMode(pci_irq_mode_t mode, uint32_t* max_irqs) {
  fbl::AutoLock dev_lock(&dev_lock_);
  switch (mode) {
    case PCI_IRQ_MODE_MSI:
      if (caps_.msi) {
        *max_irqs = caps_.msi->vectors_avail();
        return ZX_OK;
      }
      break;
    case PCI_IRQ_MODE_DISABLED:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::SetIrqMode(pci_irq_mode_t mode, uint32_t irq_cnt) {
  fbl::AutoLock dev_lock(&dev_lock_);

  if (mode >= PCI_IRQ_MODE_COUNT) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Before enabling any given interrupt mode we need to ensure no existing
  // interrupts are configured. Disabling them can fail in cases downstream
  // drivers have mot freed outstanding interrupt objects allocated off of
  // an MSI object.
  // TODO(fxb/32978): Should the protocol instead require that the driver
  // disable interrupts before requesting a new mode of operation?
  if (irqs_.mode != PCI_IRQ_MODE_DISABLED) {
    if (zx_status_t st = DisableInterrupts(); st != ZX_OK) {
      return st;
    }
  }

  switch (mode) {
    case PCI_IRQ_MODE_DISABLED:
      return DisableInterrupts();
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

zx_status_t Device::EnableMsi(uint32_t irq_cnt) {
  ZX_DEBUG_ASSERT(irqs_.mode == PCI_IRQ_MODE_DISABLED);
  ZX_DEBUG_ASSERT(caps_.msi);
  ZX_DEBUG_ASSERT(!irqs_.msi_allocation);

  if (!fbl::is_pow2(irq_cnt) || irq_cnt > caps_.msi->vectors_avail()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::msi msi;
  zx_status_t st = bdi_->AllocateMsi(irq_cnt, &msi);
  if (st != ZX_OK) {
    return st;
  }

  st = msi.get_info(ZX_INFO_MSI, &irqs_.msi_info, sizeof(irqs_.msi_info), nullptr, nullptr);
  if (st != ZX_OK) {
    return st;
  }
  ZX_DEBUG_ASSERT(irqs_.msi_info.num_irq == irq_cnt);
  ZX_DEBUG_ASSERT(irqs_.msi_info.interrupt_count == 0);

  MsiControlReg ctrl = {.value = cfg_->Read(caps_.msi->ctrl())};
  cfg_->Write(caps_.msi->tgt_addr(), irqs_.msi_info.target_addr);
  cfg_->Write(caps_.msi->tgt_data(), irqs_.msi_info.target_data);
  ctrl.set_mm_enable(MsiCapability::CountToMmc(irq_cnt));
  ctrl.set_enable(1);
  cfg_->Write(caps_.msi->ctrl(), ctrl.value);

  irqs_.msi_allocation = std::move(msi);
  irqs_.mode = PCI_IRQ_MODE_MSI;
  return ZX_OK;
}

zx_status_t Device::DisableMsi() {
  ZX_DEBUG_ASSERT(irqs_.mode = PCI_IRQ_MODE_MSI);
  ZX_DEBUG_ASSERT(caps_.msi);
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

  MsiControlReg ctrl = {.value = cfg_->Read(caps_.msi->ctrl())};
  ctrl.set_enable(0);
  cfg_->Write(caps_.msi->ctrl(), ctrl.value);

  irqs_.msi_info = {};
  irqs_.msi_allocation.reset();
  irqs_.mode = PCI_IRQ_MODE_DISABLED;
  return ZX_OK;
}

zx_status_t Device::EnableMsix(uint32_t irq_cnt) { return ZX_ERR_NOT_SUPPORTED; }
zx_status_t Device::DisableMsix() { return ZX_ERR_NOT_SUPPORTED; }

}  // namespace pci
