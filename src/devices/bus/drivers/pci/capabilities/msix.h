// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_

#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>

#include <algorithm>

#include <hwreg/bitfields.h>

#include "src/devices/bus/drivers/pci/bar_info.h"
#include "src/devices/bus/drivers/pci/capabilities.h"
#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/config.h"

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
  MsixCapability(const Config& cfg, uint8_t base);

  zx_status_t Init(const Bar& tbar, const Bar& pbar);

  PciReg16 ctrl() { return ctrl_; }
  PciReg32 table() { return table_reg_; }
  PciReg32 pba() { return pba_reg_; }
  uint8_t table_bar() const { return table_bar_; }
  uint32_t table_offset() const { return table_offset_; }
  zx::unowned_vmo table_vmo() { return table_mmio_->get_vmo(); }
  uint16_t table_size() const { return std::min(kMaxMsixVectors, table_size_); }
  uint8_t pba_bar() const { return pba_bar_; }
  uint32_t pba_offset() const { return pba_offset_; }

 private:
  // MSI-X supports up to 2048 vectors, but our system only processes vectors on
  // the bootstrap cpu. There is a real risk that a given device function can
  // exhaust our IRQ pool, though it's unlikely outside of server class
  // hardware. For now, limit an individual function to 8 vectors by reporting
  // a limited table size.
  static constexpr uint16_t kMaxMsixVectors = 8u;
  // Mapped tables for the capability. They may share the same page, but it's impossible
  // to know until runtime.
  std::optional<ddk::MmioBuffer> table_mmio_;
  std::optional<ddk::MmioBuffer> pba_mmio_;
  MMIO_PTR MsixTable* table_;
  MMIO_PTR uint64_t* pba_;
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
  bool inited_ = false;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSIX_H_
