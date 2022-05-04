// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/devices/bus/drivers/pci/capabilities/msix.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "src/devices/bus/drivers/pci/config.h"

namespace pci {

MsixCapability::MsixCapability(const Config& cfg, uint8_t base)
    : Capability(static_cast<uint8_t>(Capability::Id::kMsiX), base, cfg.addr()),
      ctrl_(PciReg16(static_cast<uint8_t>(base + kMsixControlRegisterOffset))),
      table_reg_(PciReg32(static_cast<uint8_t>(base + kMsixTableRegisterOffset))),
      pba_reg_(PciReg32(static_cast<uint8_t>(base + kMsixPbaRegisterOffset))) {
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
  size_t table_bytes = table_size_ * sizeof(MsixTable);
  // Every vector has a single bit in a large contiguous bitmask.  where the
  // smallest allocation is 64 bits.
  size_t pba_bytes = ((table_size_ / 64) + 1) * sizeof(uint64_t);
  zxlogf(TRACE, "[%s] MSI-X supports %u vector%c", addr(), table_size_,
         (table_size_ == 1) ? ' ' : 's');
  zxlogf(TRACE, "[%s] MSI-X mask table bar %u @ %#x-%#zx", addr(), table_bar_, table_offset_,
         table_offset_ + table_bytes);
  zxlogf(TRACE, "[%s] MSI-X pending table bar %u @ %#x-%#zx", addr(), pba_bar_, pba_offset_,
         pba_offset_ + pba_bytes);
  // Treat each bar as separate to simplify the configuration logic. Size checks
  // double as a way to ensure the bars are valid.
  if (tbar.size < table_offset_ + table_bytes) {
    zxlogf(ERROR, "[%s] MSI-X table doesn't fit within BAR %u size of %#zx", addr(), table_bar_,
           tbar.size);
    return ZX_ERR_BAD_STATE;
  }

  if (pbar.size < pba_offset_ + pba_bytes) {
    zxlogf(ERROR, "[%s] MSI-X pba doesn't fit within BAR %u size of %#zx", addr(), pba_bar_,
           pbar.size);
    return ZX_ERR_BAD_STATE;
  }

  zx::status<zx::vmo> result = tbar.allocation->CreateVmo();
  if (!result.is_ok()) {
    zxlogf(ERROR, "[%s] Couldn't allocate VMO for MSI-X table bar: %s", addr(),
           result.status_string());
    return result.status_value();
  }
  zx::vmo table_vmo(std::move(result.value()));

  result = pbar.allocation->CreateVmo();
  if (!result.is_ok()) {
    zxlogf(ERROR, "[%s] Couldn't allocate VMO for MSI-X pba bar: %s", addr(),
           result.status_string());
    return result.status_value();
  }
  zx::vmo pba_vmo(std::move(result.value()));

  zx_status_t st = fdf::MmioBuffer::Create(table_offset_, table_bytes, std::move(table_vmo),
                                           ZX_CACHE_POLICY_UNCACHED_DEVICE, &table_mmio_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't map MSI-X table: %s", addr(), zx_status_get_string(st));
    return st;
  }
  table_ = static_cast<MMIO_PTR MsixTable*>(table_mmio_->get());

  st = fdf::MmioBuffer::Create(pba_offset_, pba_bytes, std::move(pba_vmo),
                               ZX_CACHE_POLICY_UNCACHED_DEVICE, &pba_mmio_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "[%s] Couldn't map MSI-X pba: %s", addr(), zx_status_get_string(st));
    return st;
  }
  pba_ = static_cast<MMIO_PTR uint64_t*>(pba_mmio_->get());

  inited_ = true;
  return ZX_OK;
}

zx::status<size_t> MsixCapability::GetBarDataSize(const Bar& bar) const {
  size_t bar_size = bar.size;
  uint32_t page_size = zx_system_get_page_size();
  // In the best case, Vector and PBA tables are placed in their own BAR.
  // However, it's possible for a function to be designed so that they share a
  // BAR with device data and we need to limit the mappable space of the BAR
  // provided to the userspace driver. Additionally, if the offset of either of
  // the tables is within a page of device data we cannot allow the device to
  // map it. This arrangement would technically be against the specification, but
  // it is worth validating anyway.
  // PCI Local Bus Specification rev 3.0 6.8.2
  const std::pair<uint8_t, zx_paddr_t> sections[] = {{table_bar_, table_offset_},
                                                     {pba_bar_, pba_offset_}};
  for (auto& [bar_id, offset] : sections) {
    if (bar.bar_id != bar_id) {
      continue;
    }

    // If either of the tables are in the same page as the BAR data we cannot
    // permit access to it due to VMO granularity being equal to a page.
    if (offset < page_size) {
      return zx::error(ZX_ERR_ACCESS_DENIED);
    }

    // Truncate the size of the bar from [0, size) to [0, offset) if size is
    // larger, ensuring we cannot access it the table that shares this BAR.
    // Round down to nearest page to handle situations where a table is not on a
    // page boundary.
    bar_size = std::min(bar_size, offset);
    bar_size = (bar_size / page_size) * page_size;
  }

  return zx::ok(bar_size);
}

}  // namespace pci
