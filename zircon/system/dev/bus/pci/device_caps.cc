// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "capabilities/msi.h"
#include "capabilities/pci_express.h"
#include "common.h"
#include "device.h"

namespace pci {

// These methods provide common helper functions that are useful to both
// Capability and Extended Capability parsing since they work the same but
// differ by the widths of their register space and the valid range of their
// addresses.
namespace {

template <class RegType>
struct CapabilityHdr {
  RegType id;
  RegType ptr;
};

// |RegType| one of uint8_t or uint16_t
// |ConfigRegType| one of PciReg8 or PciReg16
template <class RegType, class ConfigRegType>
bool ReadCapability(Config& cfg, RegType offset, CapabilityHdr<RegType>* header) {
  if (offset == 0 || offset == std::numeric_limits<RegType>::max()) {
    return false;
  }

  // Read the id (at offset + 0x0) and pointer to the next cap (at offset +
  // 0x1). The lower two bits must be masked off per PCI Local Bus Spec 6.7.
  // In the case of PCIe, the ptr field also contains the revision number of
  // the capability and that can be handled in the ParseExtCapabilities()
  // method.
  header->id = cfg.Read(ConfigRegType(offset));
  header->ptr = cfg.Read(ConfigRegType(static_cast<uint16_t>(offset + sizeof(RegType))));

  // Return the pointer to the next capability based on the new pointer found
  // in this entry.
  return true;
}

// |CapabilityBaseType| one of Capability, or ExtendedCapability
template <class CapabilityBaseType>
bool CapabilityCycleExists(const Config& cfg,
                           fbl::DoublyLinkedList<std::unique_ptr<CapabilityBaseType>>* list,
                           typename CapabilityBaseType::RegType offset) {
  auto found = list->find_if([&offset](const auto& c) { return c.base() == offset; });
  if (found != list->end()) {
    pci_errorf("%s found cycle in capabilities, disabling device: ", cfg.addr());
    bool first = true;
    for (auto& cap = found; cap != list->end(); cap++) {
      if (!first) {
        zxlogf(ERROR, " -> ");
      } else {
        first = false;
      }
      zxlogf(ERROR, "%#x", cap->base());
    }
    zxlogf(ERROR, " -> %#x\n", offset);
    return true;
  }

  return false;
}

// |CapabilityType| one of the values in Capability::Ids or ExtendedCapability::Ids
template <class CapabilityType>
zx_status_t AllocateCapability(
    uint16_t offset, const Config& cfg, CapabilityType** out,
    fbl::DoublyLinkedList<std::unique_ptr<typename CapabilityType::BaseClass>>* list) {
  // If we find a duplicate of a singleton capability then either we've parsed incorrectly,
  // or the device configuration space is suspect.
  if (*out != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  auto new_cap =
      std::make_unique<CapabilityType>(cfg, static_cast<typename CapabilityType::RegType>(offset));
  *out = new_cap.get();
  list->push_back(std::move(new_cap));
  return ZX_OK;
}

}  // namespace

zx_status_t Device::ParseCapabilities() {
  // Our starting point comes from the Capability Pointer in the config header.
  struct CapabilityHdr<uint8_t> hdr;
  auto cap_offset = cfg_->Read(Config::kCapabilitiesPtr);
  if (!cap_offset) {
    return ZX_OK;
  }

  // Walk the pointer list for the standard capabilities table. Check for
  // cycles and invalid pointers.
  while (ReadCapability<uint8_t, PciReg8>(*cfg_, cap_offset, &hdr)) {
    pci_tracef("%s capability %s(%#02x) @ %#02x. Next is %#02x\n", cfg_->addr(),
               CapabilityIdToName(static_cast<Capability::Id>(hdr.id)), hdr.id, cap_offset,
               hdr.ptr);

    if (CapabilityCycleExists<Capability>(*cfg_, &caps_.list, cap_offset)) {
      pci_tracef("%s capability cycle detected\n", cfg_->addr());
      return ZX_ERR_BAD_STATE;
    }

    // Depending on the capability found we allocate a structure of the
    // appropriate type and add it to the bookkeeping tree. For important
    // things like MSI & PCIE we'll cache a raw pointer to it for fast
    // access, but otherwise everything is found via the capability list.
    zx_status_t st;
    switch (static_cast<Capability::Id>(hdr.id)) {
      case Capability::Id::kPciExpress:
        st = AllocateCapability<PciExpressCapability>(cap_offset, *cfg_, &caps_.pcie, &caps_.list);
        if (st != ZX_OK) {
          pci_tracef("%s Error allocating PCIe capability: %d, %p\n", cfg_->addr(), st, caps_.pcie);
          return st;
        }
        break;
      case Capability::Id::kMsi:
        st = AllocateCapability<MsiCapability>(cap_offset, *cfg_, &caps_.msi, &caps_.list);
        if (st != ZX_OK) {
          pci_tracef("%s Error allocating MSI capability: %d, %p\n", cfg_->addr(), st, caps_.msi);
          return st;
        }
        break;
      case Capability::Id::kNull:
      case Capability::Id::kPciPowerManagement:
      case Capability::Id::kAgp:
      case Capability::Id::kVpd:
      case Capability::Id::kSlotIdentification:
      case Capability::Id::kCompactPciHotSwap:
      case Capability::Id::kPciX:
      case Capability::Id::kHyperTransport:
      case Capability::Id::kVendor:
      case Capability::Id::kDebugPort:
      case Capability::Id::kCompactPciCrc:
      case Capability::Id::kPciHotplug:
      case Capability::Id::kPciBridgeSubsystemVendorId:
      case Capability::Id::kAgp8x:
      case Capability::Id::kSecureDevice:
      case Capability::Id::kMsiX:
      case Capability::Id::kSataDataNdxCfg:
      case Capability::Id::kAdvancedFeatures:
      case Capability::Id::kEnhancedAllocation:
      case Capability::Id::kFlatteningPortalBridge:
        caps_.list.push_back(std::make_unique<Capability>(Capability(hdr.id, cap_offset)));
        break;
    }

    cap_offset = hdr.ptr & 0xFC;  // Lower two bits are reserved.
    if (cap_offset && (cap_offset < PCI_CAP_PTR_MIN_VALID || cap_offset > PCI_CAP_PTR_MAX_VALID)) {
      pci_errorf("%s capability pointer out of range: %#02x, disabling device\n", cfg_->addr(),
                 cap_offset);
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  return ZX_OK;
}

// Parse PCI Standard Capabilities starting with the pointer in the PCI
// config structure.
zx_status_t Device::ProbeCapabilities() {
  zx_status_t st = ParseCapabilities();
  if (st != ZX_OK) {
    return st;
  }

  // TODO(ZX-3146): Implement extended capabilities
  return ZX_OK;
}

}  // namespace pci
