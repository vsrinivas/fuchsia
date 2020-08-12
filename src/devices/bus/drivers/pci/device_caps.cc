// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fbl/string_buffer.h>
#include <hwreg/bitfields.h>

#include "capabilities/msi.h"
#include "capabilities/msix.h"
#include "capabilities/pci_express.h"
#include "common.h"
#include "device.h"
namespace pci {

struct CapabilityHdr {
  uint8_t id;
  uint8_t ptr;
};

bool ReadCapability(Config& cfg, uint8_t offset, CapabilityHdr* header) {
  if (offset == 0 || (offset & 0xfc) == 0xfc) {
    return false;
  }

  // Read the id (at offset + 0x0) and pointer to the next cap (at offset + 1
  // The lower two bits must be masked off per PCI Local Bus Spec 6.7.
  // In the case of PCIe, the ptr field also contains the revision number of
  // the capability and that can be handled in the ParseExtCapabilities()
  // method.
  header->id = cfg.Read(PciReg8(offset));
  header->ptr = cfg.Read(PciReg8(static_cast<uint16_t>(offset + 1u))) & static_cast<uint8_t>(~0x3);
  return true;
}

// PCI Express Base Spec 7.6
struct ExtCapabilityIdReg {
  uint32_t value;
  DEF_SUBFIELD(value, 31, 20, offset);
  DEF_SUBFIELD(value, 19, 16, version);
  DEF_SUBFIELD(value, 15, 0, id);
};

struct ExtCapabilityHdr {
  uint16_t id;
  uint16_t ptr;
  uint8_t version;
};

bool ReadExtCapability(Config& cfg, uint16_t offset, ExtCapabilityHdr* header) {
  if (offset == 0 || (offset & 0xffc) == 0xffc) {
    return false;
  }

  ExtCapabilityIdReg reg = {.value = cfg.Read(PciReg32(offset))};
  uint16_t id = static_cast<uint16_t>(reg.id());
  if (id == 0xffff) {
    return false;
  }

  // Extended capabilities start with a 16 bit id, followed by a 4 bit version
  // and a 12 bit pointer to the next offset. Like standard capabilities, the bottom
  // 2 bits off the offset must be masked off.
  header->id = id;
  header->ptr = static_cast<uint16_t>(reg.offset() & ~0x3);
  header->version = static_cast<uint8_t>(reg.version());
  return true;
}

namespace {
// The methods here provide common helper functions that are useful to both
// Capability and Extended Capability parsing since they work the same but
// differ by the widths of their register space and the valid range of their
// addresses.

// |CapabilityBaseType| one of Capability, or ExtendedCapability
template <class CapabilityBaseType>
bool CapabilityCycleExists(const Config& cfg,
                           fbl::DoublyLinkedList<std::unique_ptr<CapabilityBaseType>>* list,
                           typename CapabilityBaseType::RegType offset) {
  auto found = list->find_if([&offset](const auto& c) { return c.base() == offset; });
  if (found != list->end()) {
    fbl::StringBuffer<256> log;
    log.AppendPrintf("%s found cycle in capabilities, disabling device: ", cfg.addr());
    bool first = true;
    for (auto& cap = found; cap != list->end(); cap++) {
      if (!first) {
        log.AppendPrintf(" -> ");
      } else {
        first = false;
      }
      log.AppendPrintf("%#x", cap->base());
    }
    log.AppendPrintf(" -> %#x", offset);
    zxlogf(ERROR, "%s", log.c_str());
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

zx_status_t Device::ConfigureCapabilities() {
  fbl::AutoLock dev_lock(&dev_lock_);
  zx_status_t st;
  if (caps_.msix) {
    auto& msix = *caps_.msix;
    st = msix.Init(bars()[msix.table_bar()], bars()[msix.pba_bar()]);
    if (st != ZX_OK) {
      zxlogf(ERROR, "Failed to initialize MSI-X: %d", st);
      return st;
    }
  }

  return ZX_OK;
}

zx_status_t Device::ParseCapabilities() {
  // Our starting point comes from the Capability Pointer in the config header.
  struct CapabilityHdr hdr;
  auto cap_offset = cfg_->Read(Config::kCapabilitiesPtr);
  if (!cap_offset) {
    return ZX_OK;
  }

  // Walk the pointer list for the standard capabilities table. Check for
  // cycles and invalid pointers.
  while (ReadCapability(*cfg_, cap_offset, &hdr)) {
    zxlogf(DEBUG, "[%s] capability %s(%#02x) @ %#02x. Next is %#02x", cfg_->addr(),
           CapabilityIdToName(static_cast<Capability::Id>(hdr.id)), hdr.id, cap_offset, hdr.ptr);

    if (CapabilityCycleExists<Capability>(*cfg_, &caps_.list, cap_offset)) {
      zxlogf(ERROR, "%s capability cycle detected", cfg_->addr());
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
          zxlogf(ERROR, "%s Error allocating PCIe capability: %d, %p", cfg_->addr(), st,
                 caps_.pcie);
          return st;
        }
        break;
      case Capability::Id::kMsi:
        st = AllocateCapability<MsiCapability>(cap_offset, *cfg_, &caps_.msi, &caps_.list);
        if (st != ZX_OK) {
          zxlogf(ERROR, "%s Error allocating MSI capability: %d, %p", cfg_->addr(), st, caps_.msi);
          return st;
        }
        break;
      case Capability::Id::kMsiX:
        st = AllocateCapability<MsixCapability>(cap_offset, *cfg_, &caps_.msix, &caps_.list);
        if (st != ZX_OK) {
          zxlogf(ERROR, "%s Error allocating MSI-X capability: %d, %p", cfg_->addr(), st,
                 caps_.msix);
          return st;
        }
        break;
      case Capability::Id::kNull:
      case Capability::Id::kPciPowerManagement:
      case Capability::Id::kAgp:
      case Capability::Id::kVitalProductData:
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
      case Capability::Id::kSataDataNdxCfg:
      case Capability::Id::kAdvancedFeatures:
      case Capability::Id::kEnhancedAllocation:
      case Capability::Id::kFlatteningPortalBridge:
        caps_.list.push_back(std::make_unique<Capability>(hdr.id, cap_offset));
        break;
    }

    cap_offset = hdr.ptr;
    if (cap_offset && (cap_offset < PCI_CAP_PTR_MIN_VALID || cap_offset > PCI_CAP_PTR_MAX_VALID)) {
      zxlogf(ERROR, "%s capability pointer out of range: %#02x, disabling device", cfg_->addr(),
             cap_offset);
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  return ZX_OK;
}

zx_status_t Device::ParseExtendedCapabilities() {
  // Extended capabilities always start at offset 256, the first byte in extended
  // configuration space.
  struct ExtCapabilityHdr hdr;
  uint16_t cap_offset = PCIE_EXT_CAP_BASE_PTR;

  // Walk the pointer list for the standard capabilities table. Check for
  // cycles and invalid pointers.
  while (ReadExtCapability(*cfg_, cap_offset, &hdr)) {
    zxlogf(TRACE, "%s ext_capability %s(%#02x) @ %#02x. Next is %#02x", cfg_->addr(),
           ExtCapabilityIdToName(static_cast<ExtCapability::Id>(hdr.id)), hdr.id, cap_offset,
           hdr.ptr);

    if (CapabilityCycleExists<ExtCapability>(*cfg_, &caps_.ext_list, cap_offset)) {
      zxlogf(TRACE, "%s ext_capability cycle detected", cfg_->addr());
      return ZX_ERR_BAD_STATE;
    }

    // Depending on the capability found we allocate a structure of the
    // appropriate type and add it to the bookkeeping tree. For important
    // things like MSI & PCIE we'll cache a raw pointer to it for fast
    // access, but otherwise everything is found via the capability list.
    switch (static_cast<ExtCapability::Id>(hdr.id)) {
      case ExtCapability::Id::kNull:
      case ExtCapability::Id::kAdvancedErrorReporting:
      case ExtCapability::Id::kVirtualChannelNoMFVC:
      case ExtCapability::Id::kDeviceSerialNumber:
      case ExtCapability::Id::kPowerBudgeting:
      case ExtCapability::Id::kRootComplexLinkDeclaration:
      case ExtCapability::Id::kRootComplexInternalLinkControl:
      case ExtCapability::Id::kRootComplexEventCollectorEndpointAssociation:
      case ExtCapability::Id::kMultiFunctionVirtualChannel:
      case ExtCapability::Id::kVirtualChannel:
      case ExtCapability::Id::kRCRB:
      case ExtCapability::Id::kVendor:
      case ExtCapability::Id::kCAC:
      case ExtCapability::Id::kACS:
      case ExtCapability::Id::kARI:
      case ExtCapability::Id::kATS:
      case ExtCapability::Id::kSR_IOV:
      case ExtCapability::Id::kMR_IOV:
      case ExtCapability::Id::kMulticast:
      case ExtCapability::Id::kPRI:
      case ExtCapability::Id::kEnhancedAllocation:
      case ExtCapability::Id::kResizableBAR:
      case ExtCapability::Id::kDynamicPowerAllocation:
      case ExtCapability::Id::kTPHRequester:
      case ExtCapability::Id::kLatencyToleranceReporting:
      case ExtCapability::Id::kSecondaryPCIExpress:
      case ExtCapability::Id::kPMUX:
      case ExtCapability::Id::kPASID:
      case ExtCapability::Id::kLNR:
      case ExtCapability::Id::kDPC:
      case ExtCapability::Id::kL1PMSubstates:
      case ExtCapability::Id::kPrecisionTimeMeasurement:
      case ExtCapability::Id::kMPCIe:
      case ExtCapability::Id::kFRSQueueing:
      case ExtCapability::Id::kReadinessTimeReporting:
      case ExtCapability::Id::kDesignatedVendor:
      case ExtCapability::Id::kVFResizableBAR:
      case ExtCapability::Id::kDataLinkFeature:
      case ExtCapability::Id::kPhysicalLayer16:
      case ExtCapability::Id::kLaneMarginingAtReceiver:
      case ExtCapability::Id::kHierarchyId:
      case ExtCapability::Id::kNativePCIeEnclosure:
      case ExtCapability::Id::kPhysicalLayer32:
      case ExtCapability::Id::kAlternateProtocol:
      case ExtCapability::Id::kSystemFirmwareIntermediary:
        caps_.ext_list.push_back(std::make_unique<ExtCapability>(hdr.id, hdr.version, cap_offset));
        break;
    }

    cap_offset = hdr.ptr;
    if (cap_offset &&
        (cap_offset < PCIE_EXT_CAP_PTR_MIN_VALID || cap_offset > PCIE_EXT_CAP_PTR_MAX_VALID)) {
      zxlogf(ERROR, "%s ext_capability pointer out of range: %#02x, disabling device", cfg_->addr(),
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

  st = ParseExtendedCapabilities();
  if (st != ZX_OK) {
    return st;
  }
  return ZX_OK;
}

}  // namespace pci
