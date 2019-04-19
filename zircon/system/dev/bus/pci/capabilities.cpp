// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "capabilities.h"

namespace pci {

// TODO(cja): Remove when lspci is supported.
// clang-format off
const char* CapabilityIdToName(Capability::Id id) {
    switch(id) {
    case Capability::Id::kNull:                       return "Null";
    case Capability::Id::kPciPowerManagement:         return "PCI Power Management";
    case Capability::Id::kAgp:                        return "AGP";
    case Capability::Id::kVpd:                        return "VPD";
    case Capability::Id::kSlotIdentification:         return "Slot Identification";
    case Capability::Id::kMsi:                        return "MSI";
    case Capability::Id::kCompactPciHotSwap:          return "CompactPCI Hotswap";
    case Capability::Id::kPciX:                       return "PCI-X";
    case Capability::Id::kHyperTransport:             return "HyperTransport";
    case Capability::Id::kVendor:                     return "Vendor";
    case Capability::Id::kDebugPort:                  return "Debug Port";
    case Capability::Id::kCompactPciCrc:              return "CompactPCI CRC";
    case Capability::Id::kPciHotplug:                 return "PCI Hotplug";
    case Capability::Id::kPciBridgeSubsystemVendorId: return "PCI Bridge Subsystem VID";
    case Capability::Id::kAgp8x:                      return "AGP 8x";
    case Capability::Id::kSecureDevice:               return "Secure Device";
    case Capability::Id::kPciExpress:                 return "PCI Express";
    case Capability::Id::kMsiX:                       return "MSI-X";
    case Capability::Id::kSataDataNdxCfg:             return "SATA Data Ndx Config";
    case Capability::Id::kAdvancedFeatures:           return "Advanced Features";
    case Capability::Id::kEnhancedAllocation:         return "Enhanced Allocations";
    case Capability::Id::kFlatteningPortalBridge:     return "Flattening Portal Bridge";
    }

    // If we reached this point we're not sure what we found.
    return "Unknown";
}
// clang-format on

} // namespace pci
