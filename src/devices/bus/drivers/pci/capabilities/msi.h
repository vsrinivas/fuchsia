// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSI_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSI_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <zircon/assert.h>

#include <hwreg/bitfields.h>

#include "src/devices/bus/drivers/pci/capabilities.h"

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
  static constexpr size_t kMaxMsiVectors = 32u;
  // These methods convert from the mm_capable register values to irq count and
  // back. The register stores the nth power of two rather than the count itself
  // to save bits, but it's easier to lean on the compiler here than use pow()
  // methods. PCI Local Bus Specification v3.0 section 6.8.1.3.
  static constexpr uint8_t MmcToCount(uint16_t reg_value) {
    switch (reg_value) {
      case 0x000:
        return 1;
      case 0b001:
        return 2;
      case 0b010:
        return 4;
      case 0b011:
        return 8;
      case 0b100:
        return 16;
      case 0b101:
        return 32;
    }
    zxlogf(ERROR, "Invalid mm_capable value read: %#x\n", reg_value);
    return 1;
  }

  static constexpr uint8_t CountToMmc(uint16_t count) {
    switch (count) {
      case 1:
        return 0b000;
      case 2:
        return 0b001;
      case 4:
        return 0b010;
      case 8:
        return 0b011;
      case 16:
        return 0b100;
      case 32:
        return 0b101;
      default:
        ZX_PANIC("Invalid mm_capable value = %#x\n", count);
    }
  }

  MsiCapability(const Config& cfg, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kMsi), base, cfg.addr()),
        ctrl_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        tgt_addr_(PciReg32(static_cast<uint16_t>(base + 0x4))),
        // In all 64 bit layouts the upper address bits are at base + 0x8
        tgt_addr_upper_(PciReg32(static_cast<uint16_t>(base + 0x8))) {
    // MSI has a structure layout that varies based on whether it supports
    // 64 bit address writes and per vector masking. Since there are four
    // possible layouts we need to determine the register offsets via probing.
    MsiControlReg ctrl = {.value = cfg.Read(ctrl_)};
    vectors_avail_ = MmcToCount(ctrl.mm_capable());
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
  PciReg16 tgt_data() { return tgt_data_; }

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

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_MSI_H_
