// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_MSI_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_MSI_H_

#include <hwreg/bitfields.h>

#include "../capabilities.h"

namespace pci {

struct MsiControlReg {
  uint16_t value;
  DEF_SUBBIT(value, 0, enable);
  DEF_SUBFIELD(value, 3, 1, mm_capable);
  DEF_SUBFIELD(value, 6, 4, mm_enable);
  DEF_SUBBIT(value, 7, is_64bit_capable);
  DEF_SUBBIT(value, 8, is_pvm_capable);
};

// PCI Local Bus Spec 6.8.1: MSI Capability Structure.
class MsiCapability : public Capability {
 public:
  MsiCapability(const Config& cfg, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kMsi), base),
        ctrl_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        tgt_addr_(PciReg32(static_cast<uint16_t>(base + 0x4))),
        // In all 64 bit layouts the upper address bits are at base + 0x8
        tgt_addr_upper_(PciReg32(static_cast<uint16_t>(base + 0x8))) {
    // MSI has a structure layout that varies based on whether it supports
    // 64 bit address writes and per vector masking. Since there are four
    // possible layouts we need to determine the register offsets via probing.
    MsiControlReg ctrl = {.value = cfg.Read(ctrl_)};
    vectors_avail_ = static_cast<uint8_t>((ctrl.mm_capable()) ? ctrl.mm_capable() << 1 : 1);
    supports_pvm_ = ctrl.is_pvm_capable();
    is_64bit_ = ctrl.is_64bit_capable();

    if (is_64bit_) {
      tgt_data_ = PciReg16(static_cast<uint16_t>(base + 0xC));
      if (supports_pvm_) {
        mask_bits_ = PciReg32(static_cast<uint16_t>(base + 0x10));
        pending_bits_ = PciReg32(static_cast<uint16_t>(base + 0x14));
      }
    } else {
      tgt_data_ = PciReg16(static_cast<uint16_t>(base + 0x8));
      if (supports_pvm_) {
        mask_bits_ = PciReg32(static_cast<uint16_t>(base + 0xC));
        pending_bits_ = PciReg32(static_cast<uint16_t>(base + 0x10));
      }
    }
  }

  PciReg16 ctrl() { return ctrl_; }
  PciReg32 tgt_addr() { return tgt_addr_; }
  PciReg32 tgt_addr_upper() {
    ZX_DEBUG_ASSERT(is_64bit_);
    return tgt_addr_upper_;
  }

  PciReg32 mask_bits() {
    ZX_DEBUG_ASSERT(supports_pvm_);
    return mask_bits_;
  }

  PciReg32 pending_bits() {
    ZX_DEBUG_ASSERT(supports_pvm_);
    return pending_bits_;
  }

  uint8_t vectors_avail() { return vectors_avail_; }
  bool supports_pvm() { return supports_pvm_; }
  bool is_64bit() { return is_64bit_; }

 private:
  const PciReg16 ctrl_;
  const PciReg32 tgt_addr_;
  const PciReg32 tgt_addr_upper_;
  // These values can only be determined at runtime based on the capability layout.
  PciReg16 tgt_data_;
  PciReg32 mask_bits_;
  PciReg32 pending_bits_;
  uint8_t vectors_avail_;
  bool supports_pvm_;
  bool is_64bit_;
};

}  // namespace pci

#endif  // ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_MSI_H_
