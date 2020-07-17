// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_

#include <hwreg/bitfields.h>

#include "../bar_info.h"
#include "../capabilities.h"
#include "../common.h"

namespace pci {

struct MsixControlReg {
  uint16_t value;
  DEF_SUBBIT(value, 15, enable);
  DEF_SUBBIT(value, 14, function_mask);
  DEF_SUBFIELD(value, 10, 0, table_size);
};

struct MsixTableReg : hwreg::RegisterBase<MsixTableReg, uint32_t> {
  DEF_UNSHIFTED_FIELD(31, 3, offset);
  DEF_FIELD(2, 0, bir);

  static auto Get() { return hwreg::RegisterAddr<MsixTableReg>(0); }
};

struct MsixPbaReg : hwreg::RegisterBase<MsixPbaReg, uint32_t> {
  DEF_UNSHIFTED_FIELD(31, 3, offset);
  DEF_FIELD(2, 0, bir);

  static auto Get() { return hwreg::RegisterAddr<MsixPbaReg>(0); }
};

struct MsixTable {
  uint32_t msg_addr;
  uint32_t msg_upper_addr;
  uint32_t msg_data;
  uint32_t vector_ctrl;
};
static_assert(sizeof(MsixTable) == 16);

// PCI Local Bus Spec 6.8.2: MSI-X Capability and Table Structure.
class MsixCapability : public Capability {
 public:
  MsixCapability(const Config& cfg, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kMsiX), base, cfg.addr()),
        ctrl_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        table_reg_(PciReg32(static_cast<uint16_t>(base + 0x4))),
        pba_reg_(PciReg32(static_cast<uint16_t>(base + 0x8))) {
    MsixControlReg ctrl = {.value = cfg.Read(ctrl_)};
    function_mask_ = ctrl.function_mask();
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

  zx_status_t Init(const BarInfo& tbar, const BarInfo& pbar) {
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
    // TODO(fxb/56253): Add MMIO_PTR to cast.
    table_ = static_cast<MsixTable*>((void*)table_mmio_->get());

    st = ddk::MmioBuffer::Create(pba_offset_, pba_bytes, std::move(pba_vmo),
                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &pba_mmio_);
    if (st != ZX_OK) {
      zxlogf(ERROR, "[%s] Couldn't map MSI-X pba: %d", addr(), st);
      return st;
    }
    // TODO(fxb/56253): Add MMIO_PTR to cast.
    pba_ = static_cast<uint64_t*>((void*)pba_mmio_->get());
    return ZX_OK;
  }

  const PciReg16 ctrl() { return ctrl_; }
  const PciReg32 table() { return table_reg_; }
  const PciReg32 pba() { return pba_reg_; }
  uint8_t table_bar() { return table_bar_; }
  uint32_t table_offset() { return table_offset_; }
  uint16_t table_size() { return table_size_; }
  uint8_t pba_bar() { return pba_bar_; }
  uint32_t pba_offset() { return pba_offset_; }
  // True if masking is limited to the entire function rather than per vector
  bool function_mask() { return function_mask_; }

 private:
  // Mapped tables for the capability. They may share the same page, but it's impossible
  // to know until runtime.
  std::optional<ddk::MmioBuffer> table_mmio_;
  std::optional<ddk::MmioBuffer> pba_mmio_;
  MsixTable* table_;
  uint64_t* pba_;
  // Registers for capability configuration and control.
  const PciReg16 ctrl_;
  const PciReg32 table_reg_;
  const PciReg32 pba_reg_;
  // Read-only values cached at initialization.
  uint32_t table_offset_;
  uint32_t pba_offset_;
  uint16_t table_size_;
  uint8_t table_bar_;
  uint8_t pba_bar_;
  bool function_mask_;
  bool inited_ = false;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_
