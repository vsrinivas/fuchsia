// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/bus/drivers/pci/capabilities/msix.h"

#include "src/devices/bus/drivers/pci/config.h"

namespace pci {

MsixCapability::MsixCapability(const Config& cfg, uint8_t base)
    : Capability(static_cast<uint8_t>(Capability::Id::kMsiX), base, cfg.addr()),
      ctrl_(PciReg16(static_cast<uint16_t>(base + 0x2))),
      table_reg_(PciReg32(static_cast<uint16_t>(base + 0x4))),
      pba_reg_(PciReg32(static_cast<uint16_t>(base + 0x8))) {
  MsixControlReg ctrl = {.value = cfg.Read(ctrl_)};
  // Table size is stored in the register as N-1 (PCIe Base Spec 7.7.2.2)
  table_size_ = static_cast<uint16_t>(ctrl.table_size() + 1);

  // Offset assumes a full 32bit width and is handled by the unshifted
  // field in the register structures.
  auto table = MsixTableReg::Get().FromValue(cfg.Read(table_reg_));
  table_bar_ = static_cast<uint8_t>(table.bir());
  table_offset_ = table.offset();

  auto pba = MsixPbaReg::Get().FromValue(cfg.Read(pba_reg_));
  pba_bar_ = static_cast<uint8_t>(pba.bir());
  pba_offset_ = pba.offset();
}

zx_status_t MsixCapability::Init(const Bar& tbar, const Bar& pbar) {
  if (inited_) {
    return ZX_ERR_BAD_STATE;
  }

  // Every vector has one entry in the table and in the pending bit array.
  size_t tbl_bytes = table_size_ * sizeof(MsixTable);
  // Every vector has a single bit in a large contiguous bitmask.
  // where the smallest allocation is 64 bits.
  size_t pba_bytes = ((table_size_ / 64) + 1) * sizeof(uint64_t);
  zxlogf(TRACE, "[%s] MSI-X supports %u vector%c", addr(), table_size_,
         (table_size_ == 1) ? ' ' : 's');
  zxlogf(TRACE, "[%s] MSI-X mask table bar %u @ %#x-%#zx", addr(), table_bar_, table_offset_,
         table_offset_ + tbl_bytes);
  zxlogf(TRACE, "[%s] MSI-X pending table bar %u @ %#x-%#zx", addr(), pba_bar_, pba_offset_,
         pba_offset_ + pba_bytes);
  // Treat each bar as separate to simplify the configuration logic. Size checks
  // double as a way to ensure the bars are valid.
  if (tbar.size < table_offset_ + tbl_bytes) {
    zxlogf(ERROR, "[%s] MSI-X table doesn't fit within BAR %u size of %#zx", addr(), table_bar_,
           tbar.size);
    return ZX_ERR_BAD_STATE;
  }

  if (pbar.size < pba_offset_ + pba_bytes) {
    zxlogf(ERROR, "[%s] MSI-X pba doesn't fit within BAR %u size of %#zx", addr(), pba_bar_,
           pbar.size);
    return ZX_ERR_BAD_STATE;
  }

  zx::vmo table_vmo;
  zx_status_t st = tbar.allocation->CreateVmObject(&table_vmo);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't allocate VMO for MSI-X table bar: %d", addr(), st);
    return st;
  }

  zx::vmo pba_vmo;
  st = pbar.allocation->CreateVmObject(&pba_vmo);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't allocate VMO for MSI-X pba bar: %d", addr(), st);
    return st;
  }

  st = ddk::MmioBuffer::Create(table_offset_, tbl_bytes, std::move(table_vmo),
                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &table_mmio_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't map MSI-X table: %d", addr(), st);
    return st;
  }
  table_ = static_cast<MMIO_PTR MsixTable*>(table_mmio_->get());

  st = ddk::MmioBuffer::Create(pba_offset_, pba_bytes, std::move(pba_vmo),
                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &pba_mmio_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't map MSI-X pba: %d", addr(), st);
    return st;
  }
  pba_ = static_cast<MMIO_PTR uint64_t*>(pba_mmio_->get());
  return ZX_OK;
}

}  // namespace pci
