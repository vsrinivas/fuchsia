// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_

#include "config.h"
#include <fbl/intrusive_double_list.h>
#include <memory>

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

    Capability(uint8_t id, uint8_t base)
        : id_(id), base_(base) {}
    uint8_t id() const { return id_; }
    uint8_t base() const { return base_; }

private:
    uint8_t id_;
    uint8_t base_;
};
using CapabilityList = fbl::DoublyLinkedList<std::unique_ptr<Capability>>;
static_assert(static_cast<uint8_t>(Capability::Id::kFlatteningPortalBridge) == 0x15);

struct PciExpressCapability : public Capability {
    PciExpressCapability(uint8_t base)
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
} // namespace pci

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_CAPABILITIES_H_
