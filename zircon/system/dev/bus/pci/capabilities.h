// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <hwreg/bitfields.h>

#include "config.h"

namespace pci {

// General PCI/PCIe capability classes. Final calculated address
// for config corresponds to cfg's base plus cap's base along with
// the specific register's offset.
class Capability : public fbl::DoublyLinkedListable<std::unique_ptr<Capability>> {
 public:
  using BaseClass = Capability;
  using RegType = uint8_t;

  // PCI Code and ID Assignment Specification Revision 1.9 section 2.
  enum class Id : RegType {
    kNull = 0,
    kPciPowerManagement,
    kAgp,
    kVpd,
    kSlotIdentification,
    kMsi,
    kCompactPciHotSwap,
    kPciX,
    kHyperTransport,
    kVendor,
    kDebugPort,
    kCompactPciCrc,
    kPciHotplug,
    kPciBridgeSubsystemVendorId,
    kAgp8x,
    kSecureDevice,
    kPciExpress,
    kMsiX,
    kSataDataNdxCfg,
    kAdvancedFeatures,
    kEnhancedAllocation,
    kFlatteningPortalBridge,
  };

  Capability(uint8_t id, uint8_t base) : id_(id), base_(base) {}
  uint8_t id() const { return id_; }
  uint8_t base() const { return base_; }

 private:
  uint8_t id_;
  uint8_t base_;
};
using CapabilityList = fbl::DoublyLinkedList<std::unique_ptr<Capability>>;
static_assert(static_cast<uint8_t>(Capability::Id::kFlatteningPortalBridge) == 0x15);

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

// PCIe Base Spec 3.0 section 7.8. PCI Express Capability Structure
class PciExpressCapability : public Capability {
 public:
  PciExpressCapability(const Config& /*cfg*/, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kPciExpress), base),
        pcie_capabilities_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        device_capabilities_(PciReg32(static_cast<uint16_t>(base + 0x4))),
        device_control_(PciReg16(static_cast<uint16_t>(base + 0x8))),
        device_status_(PciReg16(static_cast<uint16_t>(base + 0xA))) {}

 private:
  const PciReg16 pcie_capabilities_;
  const PciReg32 device_capabilities_;
  const PciReg16 device_control_;
  const PciReg16 device_status_;
};

// General PCIe Extended capability classes. Final calculated address
// for capability register corresponds to cfg's base plus cap's base along with
// the specific register's offset.
class ExtCapability : public fbl::DoublyLinkedListable<std::unique_ptr<ExtCapability>> {
 public:
  using BaseClass = ExtCapability;
  using RegType = uint16_t;

  // PCI Code and ID Assignment Specification Revision 1.9 section 3.
  enum class Id : RegType {
    kNull = 0,
    kAdvancedErrorReporting,
    kVirtualChannelNoMFVC,
    kDeviceSerialNumber,
    kPowerBudgeting,
    kRootComplexLinkDeclaration,
    kRootComplexInternalLinkControl,
    kRootComplexEventCollectorEndpointAssociation,
    kMFVC,
    kVC,
    kRCRB,
    kVSEC,
    kCAC,
    kACS,
    kART,
    kATS,
    kSR_IOV,
    kMR_IOV,
    kMulticast,
    kPRI,
    kAmdReserved,
    kResizableBAR,
    kDPA,
    kTPM,
    kLTR,
    kSecondaryPCIExpress,
    kPMUX,
    kPASID,
    kLNR,
    kDPC,
    kL1PMSubstates,
    kPTM,
    kMPCIe,
    kFRSQueueing,
    kReadinessTimeReporting,
    kVSECDesignatedVendorExtended,
    kVFResizableBAR,
    kDataLinkFeature,
    kPhysicalLayer16,
    kLaneMarginingAtReceiver,
    kHierarchyId,
  };
};
using ExtCapabilityList = fbl::DoublyLinkedList<std::unique_ptr<ExtCapability>>;
static_assert(static_cast<uint16_t>(ExtCapability::Id::kHierarchyId) == 0x28);

const char* CapabilityIdToName(Capability::Id id);
const char* ExtCapabilityIdToName(ExtCapability::Id id);
}  // namespace pci

#endif  // ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_
