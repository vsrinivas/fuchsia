// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_H_

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <hwreg/bitfields.h>

#include "src/devices/bus/drivers/pci/config.h"

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
    /* 0x00 */ kNull = 0,
    /* 0x01 */ kPciPowerManagement,
    /* 0x02 */ kAgp,
    /* 0x03 */ kVitalProductData,
    /* 0x04 */ kSlotIdentification,
    /* 0x05 */ kMsi,
    /* 0x06 */ kCompactPciHotSwap,
    /* 0x07 */ kPciX,
    /* 0x08 */ kHyperTransport,
    /* 0x09 */ kVendor,
    /* 0x0a */ kDebugPort,
    /* 0x0b */ kCompactPciCrc,
    /* 0x0c */ kPciHotplug,
    /* 0x0d */ kPciBridgeSubsystemVendorId,
    /* 0x0e */ kAgp8x,
    /* 0x0f */ kSecureDevice,
    /* 0x10 */ kPciExpress,
    /* 0x11 */ kMsiX,
    /* 0x12 */ kSataDataNdxCfg,
    /* 0x13 */ kAdvancedFeatures,
    /* 0x14 */ kEnhancedAllocation,
    /* 0x15 */ kFlatteningPortalBridge,
  };

  Capability(uint8_t id, uint8_t base, const char* addr = nullptr)
      : id_(id), base_(base), addr_(addr) {}
  [[nodiscard]] uint8_t id() const { return id_; }
  [[nodiscard]] uint8_t base() const { return base_; }
  const char* addr() { return addr_; }

 private:
  uint8_t id_;
  uint8_t base_;
  const char* addr_;
};
using CapabilityList = fbl::DoublyLinkedList<std::unique_ptr<Capability>>;
static_assert(static_cast<uint8_t>(Capability::Id::kFlatteningPortalBridge) == 0x15);

// General PCIe Extended capability classes. Final calculated address
// for capability register corresponds to cfg's base plus cap's base along with
// the specific register's offset.
class ExtCapability : public fbl::DoublyLinkedListable<std::unique_ptr<ExtCapability>> {
 public:
  using BaseClass = ExtCapability;
  using RegType = uint16_t;

  // PCI Code and ID Assignment Specification Revision 1.9 section 3.
  enum class Id : RegType {
    /* 0x00 */ kNull = 0,
    /* 0x01 */ kAdvancedErrorReporting,
    /* 0x02 */ kVirtualChannelNoMFVC,
    /* 0x03 */ kDeviceSerialNumber,
    /* 0x04 */ kPowerBudgeting,
    /* 0x05 */ kRootComplexLinkDeclaration,
    /* 0x06 */ kRootComplexInternalLinkControl,
    /* 0x07 */ kRootComplexEventCollectorEndpointAssociation,
    /* 0x08 */ kMultiFunctionVirtualChannel,
    /* 0x09 */ kVirtualChannel,
    /* 0x0a */ kRCRB,
    /* 0x0b */ kVendor,
    /* 0x0c */ kCAC,
    /* 0x0d */ kACS,
    /* 0x0e */ kARI,
    /* 0x0f */ kATS,
    /* 0x10 */ kSR_IOV,
    /* 0x11 */ kMR_IOV,
    /* 0x12 */ kMulticast,
    /* 0x13 */ kPRI,
    /* 0x14 */ kEnhancedAllocation,
    /* 0x15 */ kResizableBAR,
    /* 0x16 */ kDynamicPowerAllocation,
    /* 0x17 */ kTPHRequester,
    /* 0x18 */ kLatencyToleranceReporting,
    /* 0x19 */ kSecondaryPCIExpress,
    /* 0x1a */ kPMUX,
    /* 0x1b */ kPASID,
    /* 0x1c */ kLNR,
    /* 0x1d */ kDPC,
    /* 0x1e */ kL1PMSubstates,
    /* 0x1f */ kPrecisionTimeMeasurement,
    /* 0x20 */ kMPCIe,
    /* 0x21 */ kFRSQueueing,
    /* 0x22 */ kReadinessTimeReporting,
    /* 0x23 */ kDesignatedVendor,
    /* 0x24 */ kVFResizableBAR,
    /* 0x25 */ kDataLinkFeature,
    /* 0x26 */ kPhysicalLayer16,
    /* 0x27 */ kLaneMarginingAtReceiver,
    /* 0x28 */ kHierarchyId,
    /* 0x29 */ kNativePCIeEnclosure,
    /* 0x2a */ kPhysicalLayer32,
    /* 0x2b */ kAlternateProtocol,
    /* 0x2c */ kSystemFirmwareIntermediary,
  };

  ExtCapability(uint16_t id, uint8_t version, uint16_t base)
      : id_(id), base_(base), version_(version) {}
  [[nodiscard]] uint16_t id() const { return id_; }
  [[nodiscard]] uint16_t base() const { return base_; }
  [[nodiscard]] uint8_t version() const { return version_; }

 private:
  uint16_t id_;
  uint16_t base_;
  uint8_t version_;
};
using ExtCapabilityList = fbl::DoublyLinkedList<std::unique_ptr<ExtCapability>>;
static_assert(static_cast<uint16_t>(ExtCapability::Id::kSystemFirmwareIntermediary) == 0x2c);

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_H_
