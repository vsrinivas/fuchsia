// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "capabilities.h"

namespace pci {

// TODO(cja): Remove when lspci is supported.
const char* CapabilityIdToName(Capability::Id id) {
  switch (id) {
    case Capability::Id::kNull:
      return "Null";
    case Capability::Id::kPciPowerManagement:
      return "PCI Power Management";
    case Capability::Id::kAgp:
      return "AGP";
    case Capability::Id::kVitalProductData:
      return "Vital Product Data";
    case Capability::Id::kSlotIdentification:
      return "Slot Identification";
    case Capability::Id::kMsi:
      return "MSI";
    case Capability::Id::kCompactPciHotSwap:
      return "CompactPCI Hotswap";
    case Capability::Id::kPciX:
      return "PCI-X";
    case Capability::Id::kHyperTransport:
      return "HyperTransport";
    case Capability::Id::kVendor:
      return "Vendor";
    case Capability::Id::kDebugPort:
      return "Debug Port";
    case Capability::Id::kCompactPciCrc:
      return "CompactPCI CRC";
    case Capability::Id::kPciHotplug:
      return "PCI Hotplug";
    case Capability::Id::kPciBridgeSubsystemVendorId:
      return "PCI Bridge Subsystem VID";
    case Capability::Id::kAgp8x:
      return "AGP 8x";
    case Capability::Id::kSecureDevice:
      return "Secure Device";
    case Capability::Id::kPciExpress:
      return "PCI Express";
    case Capability::Id::kMsiX:
      return "MSI-X";
    case Capability::Id::kSataDataNdxCfg:
      return "SATA Data Ndx Config";
    case Capability::Id::kAdvancedFeatures:
      return "Advanced Features";
    case Capability::Id::kEnhancedAllocation:
      return "Enhanced Allocations";
    case Capability::Id::kFlatteningPortalBridge:
      return "Flattening Portal Bridge";
  }

  // If we reached this point we're not sure what we found.
  return "Unknown";
}

const char* ExtCapabilityIdToName(ExtCapability::Id id) {
  switch (id) {
    case ExtCapability::Id::kNull:
      return "Null";
    case ExtCapability::Id::kAdvancedErrorReporting:
      return "Advanced Error Reporting";
    case ExtCapability::Id::kVirtualChannelNoMFVC:
      return "Virtual Channel No MFVC";
    case ExtCapability::Id::kDeviceSerialNumber:
      return "Device Serial Number";
    case ExtCapability::Id::kPowerBudgeting:
      return "Power Budgeting";
    case ExtCapability::Id::kRootComplexLinkDeclaration:
      return "RootComplexLinkDeclaration";
    case ExtCapability::Id::kRootComplexInternalLinkControl:
      return "RootComplexInternalLinkControl";
    case ExtCapability::Id::kRootComplexEventCollectorEndpointAssociation:
      return "RootComplexEventCollectorEndpointAssociation";
    case ExtCapability::Id::kMultiFunctionVirtualChannel:
      return "MultiFunctionVirtualChannel";
    case ExtCapability::Id::kVirtualChannel:
      return "Virtual Channel";
    case ExtCapability::Id::kRCRB:
      return "RCRB";
    case ExtCapability::Id::kVendor:
      return "Vendor";
    case ExtCapability::Id::kCAC:
      return "CAC";
    case ExtCapability::Id::kACS:
      return "ACS";
    case ExtCapability::Id::kARI:
      return "ARI";
    case ExtCapability::Id::kATS:
      return "ATS";
    case ExtCapability::Id::kSR_IOV:
      return "SR_IOV";
    case ExtCapability::Id::kMR_IOV:
      return "MR_IOV";
    case ExtCapability::Id::kMulticast:
      return "Multicast";
    case ExtCapability::Id::kPRI:
      return "PRI";
    case ExtCapability::Id::kEnhancedAllocation:
      return "Enhanced Allocation";
    case ExtCapability::Id::kResizableBAR:
      return "Resizable BAR";
    case ExtCapability::Id::kDynamicPowerAllocation:
      return "Dynamic Power Allocation";
    case ExtCapability::Id::kTPHRequester:
      return "TPH Requester";
    case ExtCapability::Id::kLatencyToleranceReporting:
      return "Latency Tolerance Reporting";
    case ExtCapability::Id::kSecondaryPCIExpress:
      return "Secondary PCI Express";
    case ExtCapability::Id::kPMUX:
      return "PMUX";
    case ExtCapability::Id::kPASID:
      return "PASID";
    case ExtCapability::Id::kLNR:
      return "LNR";
    case ExtCapability::Id::kDPC:
      return "DPC";
    case ExtCapability::Id::kL1PMSubstates:
      return "L1 PM Substates";
    case ExtCapability::Id::kPrecisionTimeMeasurement:
      return "Precision Time Measurement";
    case ExtCapability::Id::kMPCIe:
      return "MPCIe";
    case ExtCapability::Id::kFRSQueueing:
      return "FRS Queueing";
    case ExtCapability::Id::kReadinessTimeReporting:
      return "Readiness Time Reporting";
    case ExtCapability::Id::kDesignatedVendor:
      return "Vendor";
    case ExtCapability::Id::kVFResizableBAR:
      return "VF Resizable BAR";
    case ExtCapability::Id::kDataLinkFeature:
      return "DataLink Feature";
    case ExtCapability::Id::kPhysicalLayer16:
      return "Physical Layer 16";
    case ExtCapability::Id::kLaneMarginingAtReceiver:
      return "Lane Margining At Receiver";
    case ExtCapability::Id::kHierarchyId:
      return "Hierarchy Id";
    case ExtCapability::Id::kNativePCIeEnclosure:
      return "Native PCIe Enclosure";
    case ExtCapability::Id::kPhysicalLayer32:
      return "Physical Layer 32";
    case ExtCapability::Id::kAlternateProtocol:
      return "Alternate Protocol";
    case ExtCapability::Id::kSystemFirmwareIntermediary:
      return "System Firmware Intermediary";
  }

  // If we reached this point we're not sure what we found.
  return "Unknown";
}

}  // namespace pci
