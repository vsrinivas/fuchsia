// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "device.h"
#include "bus.h"
#include "common.h"
#include "ref_counted.h"
#include "upstream_node.h"
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

namespace pci {

namespace { // anon namespace.  Externals do not need to know about DeviceImpl

class DeviceImpl : public Device {
public:
    static zx_status_t Create(zx_device_t* parent,
                              fbl::RefPtr<Config>&& cfg,
                              UpstreamNode* upstream,
                              BusLinkInterface* bli);

    // Implement ref counting, do not let derived classes override.
    PCI_IMPLEMENT_REFCOUNTED;

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceImpl);

protected:
    DeviceImpl(zx_device_t* parent,
               fbl::RefPtr<Config>&& cfg,
               UpstreamNode* upstream,
               BusLinkInterface* bli)
        : Device(parent, std::move(cfg), upstream, bli, false) {}
};

zx_status_t DeviceImpl::Create(zx_device_t* parent,
                               fbl::RefPtr<Config>&& cfg,
                               UpstreamNode* upstream,
                               BusLinkInterface* bli) {
    fbl::AllocChecker ac;
    auto raw_dev = new (&ac) DeviceImpl(parent, std::move(cfg), upstream, bli);
    if (!ac.check()) {
        pci_errorf("Out of memory attemping to create PCIe device %s.\n", cfg->addr());
        return ZX_ERR_NO_MEMORY;
    }

    auto dev = fbl::AdoptRef(static_cast<Device*>(raw_dev));
    zx_status_t status = raw_dev->Init();
    if (status != ZX_OK) {
        pci_errorf("Failed to initialize PCIe device (res %d)\n", status);
        return status;
    }

    bli->LinkDevice(dev);
    return ZX_OK;
}

} // namespace

Device::~Device() {
    // We should already be unlinked from the bus's device tree.
    ZX_DEBUG_ASSERT(disabled_);
    ZX_DEBUG_ASSERT(!plugged_in_);

    // Make certain that all bus access (MMIO, PIO, Bus mastering) has been
    // disabled.  Also, explicitly disable legacy IRQs.
    // TODO(cja/ZX-3147)): Only use the PCIe int disable if PCIe
    ModifyCmd(PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN, PCIE_CFG_COMMAND_INT_DISABLE);

    caps_.list.clear();
    // TODO(cja/ZX-3147): Remove this after porting is finished.
    pci_tracef("%s [%s] dtor finished\n", is_bridge() ? "bridge" : "device", cfg_->addr());
}

zx_status_t Device::Create(zx_device_t* parent,
                           fbl::RefPtr<Config>&& config,
                           UpstreamNode* upstream,
                           BusLinkInterface* bli) {
    return DeviceImpl::Create(parent, std::move(config), upstream, bli);
}

zx_status_t Device::Init() {
    fbl::AutoLock dev_lock(&dev_lock_);

    zx_status_t status = InitLocked();
    if (status != ZX_OK) {
        pci_errorf("failed to initialize device %s: %d\n", cfg_->addr(), status);
        return status;
    }

    // Things went well and the device is in a good state. Flag the device as
    // plugged in and link ourselves up to the graph. This will keep the device
    // alive as long as the Bus owns it.
    upstream_->LinkDevice(this);
    plugged_in_ = true;

    return status;
}

zx_status_t Device::InitLocked() {
    // Cache basic device info
    vendor_id_ = cfg_->Read(Config::kVendorId);
    device_id_ = cfg_->Read(Config::kDeviceId);
    class_id_ = cfg_->Read(Config::kBaseClass);
    subclass_ = cfg_->Read(Config::kSubClass);
    prog_if_ = cfg_->Read(Config::kProgramInterface);
    rev_id_ = cfg_->Read(Config::kRevisionId);

    // Disable the device in event of a failure initializing. TA is disabled
    // because it cannot track the scope of AutoCalls and their associated
    // locking semantics. The lock is grabbed by |Init| and held at this point.
    auto disable = fbl::MakeAutoCall([this]() TA_NO_THREAD_SAFETY_ANALYSIS {
        DisableLocked();
    });

    // Parse and sanity check the capabilities and extended capabilities lists
    // if they exist
    zx_status_t st = ProbeCapabilities();
    if (st != ZX_OK) {
        pci_errorf("device %s encountered an error parsing capabilities: %d\n", cfg_->addr(), st);
        return st;
    }

    // Now that we know what our capabilities are, initialize our internal IRQ
    // bookkeeping
    // TODO(cja): IRQ initialization
    disable.cancel();
    return ZX_OK;
}

zx_status_t Device::ModifyCmd(uint16_t clr_bits, uint16_t set_bits) {
    fbl::AutoLock dev_lock(&dev_lock_);
    // In order to keep internal bookkeeping coherent, and interactions between
    // MSI/MSI-X and Legacy IRQ mode safe, API users may not directly manipulate
    // the legacy IRQ enable/disable bit.  Just ignore them if they try to
    // manipulate the bit via the modify cmd API.
    // TODO(cja) This only applies to PCI(e)
    clr_bits = static_cast<uint16_t>(clr_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);
    set_bits = static_cast<uint16_t>(set_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);

    if (plugged_in_) {
        ModifyCmdLocked(clr_bits, set_bits);
        return ZX_OK;
    }

    return ZX_ERR_UNAVAILABLE;
}

void Device::ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) {
    fbl::AutoLock cmd_reg_lock(&cmd_reg_lock_);
    cfg_->Write(
        Config::kCommand,
        static_cast<uint16_t>((cfg_->Read(Config::kCommand) & ~clr_bits) | set_bits));
}

zx_status_t Device::EnableBusMaster(bool enabled) {
    if (enabled && disabled_) {
        return ZX_ERR_BAD_STATE;
    }

    return ModifyCmd(enabled ? 0 : PCI_COMMAND_BUS_MASTER_EN,
                     enabled ? PCI_COMMAND_BUS_MASTER_EN : 0);
}

zx_status_t Device::EnablePio(bool enabled) {
    if (enabled && disabled_) {
        return ZX_ERR_BAD_STATE;
    }

    return ModifyCmd(enabled ? 0 : PCI_COMMAND_IO_EN,
                     enabled ? PCI_COMMAND_IO_EN : 0);
}

zx_status_t Device::EnableMmio(bool enabled) {
    if (enabled && disabled_) {
        return ZX_ERR_BAD_STATE;
    }

    return ModifyCmd(enabled ? 0 : PCI_COMMAND_MEM_EN,
                     enabled ? PCI_COMMAND_MEM_EN : 0);
}

void Device::Disable() {
    fbl::AutoLock dev_lock(&dev_lock_);
    DisableLocked();
}

void Device::DisableLocked() {
    // Disable a device because we cannot allocate space for all of its BARs (or
    // forwarding windows, in the case of a bridge).  Flag the device as
    // disabled from here on out.
    pci_tracef("[%s]%s %s\n", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);

    // Flag the device as disabled.  Close the device's MMIO/PIO windows, shut
    // off device initiated accesses to the bus, disable legacy interrupts.
    // Basically, prevent the device from doing anything from here on out.
    disabled_ = true;
    AssignCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE);

    // Release all BAR allocations back into the pool they came from.
    for (auto& bar : bars_) {
        bar.allocation = nullptr;
    }
}

zx_status_t Device::ProbeBar(uint32_t bar_id) {
    if (bar_id >= bar_count_) {
        return ZX_ERR_INVALID_ARGS;
    }

    // If we hit an issue, or a BAR reads as all zeroes then we will bail out
    // and set the size of it to 0. This will result in us not using it further
    // during allocation.
    BarInfo& bar_info = bars_[bar_id];
    auto cleanup = fbl::MakeAutoCall([&bar_info] { bar_info.size = 0; });
    uint32_t bar_val = cfg_->Read(Config::kBar(bar_id));

    bar_info.bar_id = bar_id;
    bar_info.is_mmio = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
    bar_info.is_64bit = bar_info.is_mmio &&
                        ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
    bar_info.is_prefetchable = bar_info.is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);

    // Sanity check the read-only configuration of the BAR
    if (bar_info.is_64bit && (bar_info.bar_id == bar_count_ - 1)) {
        pci_errorf("%s has a 64bit bar in invalid position %u!\n", cfg_->addr(), bar_info.bar_id);
        return ZX_ERR_BAD_STATE;
    }

    if (bar_info.is_64bit && !bar_info.is_mmio) {
        pci_errorf("%s bar %u is 64bit but not mmio!\n", cfg_->addr(), bar_info.bar_id);
        return ZX_ERR_BAD_STATE;
    }

    if (bar_info.is_64bit && !bar_info.is_prefetchable) {
        pci_errorf("%s bar %u is misconfigured as 64bit but not prefetchable!\n", cfg_->addr(),
                   bar_info.bar_id);
        return ZX_ERR_BAD_STATE;
    }

    // Disable MMIO & PIO access while we perform the probe. We don't want the
    // addresses written during probing to conflict with anything else on the
    // bus. Note: No drivers should have access to this device's registers
    // during the probe process as the device should not have been published
    // yet. That said, there could be other (special case) parts of the system
    // accessing a devices registers at this point in time, like an early init
    // debug console or serial port. Don't make any attempt to print or log
    // until the probe operation has been completed. Hopefully these special
    // systems are quiescent at this point in time, otherwise they might see
    // some minor glitching while access is disabled.
    bool enabled = MmioEnabled() || IoEnabled();
    uint16_t cmd_backup = ReadCmdLocked();
    ModifyCmdLocked(PCI_COMMAND_MEM_EN | PCI_COMMAND_IO_EN, cmd_backup);
    uint32_t addr_mask = (bar_info.is_mmio) ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;

    // For enabled devices save the original address in the BAR. If the device
    // is enabled then we should assume the bios configured it and we should
    // attempt to retain the BAR allocation.
    if (enabled) {
        bar_info.address = bar_val & addr_mask;
    }

    // Write ones to figure out the size of the BAR
    cfg_->Write(Config::kBar(bar_id), UINT32_MAX);
    bar_val = cfg_->Read(Config::kBar(bar_id));
    // BARs that are not wired up return all zeroes on read after writing 1s
    if (bar_val == 0) {
        return ZX_OK;
    }

    uint64_t size_mask = ~(bar_val & addr_mask);
    if (bar_info.is_mmio && bar_info.is_64bit) {
        // This next BAR should not be probed/allocated on its own, so set
        // its size to zero and make it clear it's owned by the previous
        // BAR. We already verified the bar_id is valid above.
        bars_[bar_id + 1].size = 0;
        bars_[bar_id + 1].bar_id = bar_id;

        // Retain the high 32bits of the  address if the device was enabled.
        if (enabled) {
            bar_info.address = static_cast<uint64_t>(cfg_->Read(Config::kBar(bar_id + 1))) << 32;
        }

        // Get the high 32 bits of size for the 64 bit BAR by repeating the
        // steps of writing 1s and then reading the value of the next BAR.
        cfg_->Write(Config::kBar(bar_id + 1), UINT32_MAX);
        size_mask |= static_cast<uint64_t>(~cfg_->Read(Config::kBar(bar_id + 1))) << 32;
    }

    // No matter what configuration we've found, |size_mask| should contain a
    // mask representing all the valid bits that can be set in the address.
    bar_info.size = size_mask + 1;

    // Restore the original bar address values cached above if enabled coming
    // into this probe.
    if (enabled) {
        cfg_->Write(Config::kBar(bar_id), static_cast<uint32_t>(bar_info.address));
        if (bar_info.is_64bit) {
            cfg_->Write(Config::kBar(bar_id + 1), static_cast<uint32_t>(bar_info.address >> 32));
        }
    }

    // All done, re-enable IO/MMIO access that was disabled prior.
    AssignCmdLocked(cmd_backup);
    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Device::AllocateBar(uint32_t bar_id) {
    ZX_DEBUG_ASSERT(upstream_);
    ZX_DEBUG_ASSERT(bar_id < bar_count_);

    zx_status_t status;
    PciAllocator* allocator;
    BarInfo& bar_info = bars_[bar_id];
    // TODO(cja): It's possible that we may have an unlikely configuration of a prefetchable
    // window that starts below 4GB, ends above 4GB and then has a prefetchable 32bit BAR. If
    // that BAR already had an address we would request it here and be fine, but if it didn't
    // then the below code could potentially fail because it received an address that didn't fit
    // in 32 bits.
    if (bar_info.is_mmio) {
        if (bar_info.is_64bit || bar_info.is_prefetchable) {
            allocator = &upstream_->pf_mmio_regions();
        } else {
            allocator = &upstream_->mmio_regions();
        }
    } else {
        allocator = &upstream_->pio_regions();
    }

    // If we have an address it was found earlier in the probe and we'll try to
    // preserve it.
    if (bar_info.address) {
        status = allocator->GetRegion(bar_info.address, bar_info.size, &bar_info.allocation);
        if (status == ZX_OK) {
            // If we successfully grabbed the allocation then we're finished because
            // our metadata already matches what we requested from the allocator.
            pci_tracef("%s preserved BAR %u's existing allocation.\n", cfg_->addr(),
                       bar_info.bar_id);
            return ZX_OK;
        } else {
            pci_tracef("%s failed to preserve BAR %u address %lx, reallocating: %d\n",
                       cfg_->addr(), bar_info.bar_id, bar_info.address, status);
            bar_info.address = 0;
        }
    }

    // If we had no address, or we failed to preseve the address, then it's time
    // to take any allocation window possible.
    if (!bar_info.address) {
        status = allocator->GetRegion(bar_info.size, &bar_info.allocation);
        // Request a base address of zero to signal we'll take any location in
        // the window.
        if (status != ZX_OK) {
            pci_errorf("%s couldn't allocate %#zx for bar %u: %d\n", cfg_->addr(), bar_info.size,
                       bar_info.bar_id, status);
            return status;
        }
    }

    // Now write the allocated address space to the BAR.
    uint16_t cmd_backup = cfg_->Read(Config::kCommand);
    ModifyCmdLocked(PCI_COMMAND_MEM_EN | PCI_COMMAND_IO_EN, cmd_backup);
    cfg_->Write(Config::kBar(bar_id), static_cast<uint32_t>(bar_info.allocation->base()));
    if (bar_info.is_64bit) {
        uint32_t addr_hi = static_cast<uint32_t>(bar_info.allocation->base() >> 32);
        cfg_->Write(Config::kBar(bar_id + 1), addr_hi);
    }
    bar_info.address = bar_info.allocation->base();
    AssignCmdLocked(cmd_backup);

    return ZX_OK;
}

zx_status_t Device::ConfigureBars() {
    fbl::AutoLock dev_lock(&dev_lock_);
    ZX_DEBUG_ASSERT(plugged_in_);
    ZX_DEBUG_ASSERT(bar_count_ <= fbl::count_of(bars_));

    // Allocate BARs for the device
    zx_status_t status;
    // First pass, probe BARs to populate the table and grab backing allocations
    // for any BARs that have been allocated by system firmware.
    for (uint32_t bar_id = 0; bar_id < bar_count_; bar_id++) {
        status = ProbeBar(bar_id);
        if (status != ZX_OK) {
            pci_errorf("%s error probing bar %u: %d. Skipping it.\n", cfg_->addr(), bar_id,
                       status);
            continue;
        }

        // Allocate the BAR if it was successfully probed.
        if (bars_[bar_id].size) {
            status = AllocateBar(bar_id);
            if (status != ZX_OK) {
                pci_errorf("%s failed to allocate bar %u: %d\n", cfg_->addr(), bar_id, status);
            }
        }

        // If the BAR was 64bit then we need to skip the next bar holding its
        // high address bits.
        if (bars_[bar_id].is_64bit) {
            bar_id++;
        }
    }

    return ZX_OK;
}

zx_status_t Device::GetBarInfo(uint32_t bar_id, const BarInfo* out_info) const {
    if (bar_id >= bar_count_ || out_info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (disabled_) {
        return ZX_ERR_BAD_STATE;
    }

    out_info = &bars_[bar_id];
    return ZX_OK;
}

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
bool CapabilityCycleExists(Config& cfg,
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
zx_status_t AllocateCapability(uint16_t offset,
                CapabilityType** out,
                fbl::DoublyLinkedList<std::unique_ptr<typename CapabilityType::BaseClass>>* list) {
    // If we find a duplicate of a singleton capability then either we've parsed incorrectly,
    // or the device configuration space is suspect.
    if (out != nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    auto new_cap =
        std::make_unique<CapabilityType>(static_cast<typename CapabilityType::RegType>(offset));
    *out = new_cap.get();
    list->push_back(std::move(new_cap));
    return ZX_OK;
}

} // namespace

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
            return ZX_ERR_BAD_STATE;
        }

        // Depending on the capability found we allocate a structure of the
        // appropriate type and add it to the bookkeeping tree. For important
        // things like MSI & PCIE we'll cache a raw pointer to it for fast
        // access, but otherwise everything is found via the capability list.
        zx_status_t st;
        switch (static_cast<Capability::Id>(hdr.id)) {
        case Capability::Id::kPciExpress:
            st = AllocateCapability<PciExpressCapability>(cap_offset, &caps_.pcie, &caps_.list);
            if (st != ZX_OK) {
                return st;
            }
            break;
        case Capability::Id::kNull:
        case Capability::Id::kPciPowerManagement:
        case Capability::Id::kAgp:
        case Capability::Id::kVpd:
        case Capability::Id::kSlotIdentification:
        case Capability::Id::kMsi:
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

        cap_offset = hdr.ptr & 0xFC; // Lower two bits are reserved.
        if (cap_offset && (cap_offset < PCI_CAP_PTR_MIN_VALID || cap_offset > PCI_CAP_PTR_MAX_VALID)) {
            pci_errorf("%s capability pointer out of range: %#02x, disabling device\n",
                       cfg_->addr(), cap_offset);
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

void Device::Unplug() {
    pci_tracef("[%s]%s %s\n", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);
    // Begin by completely nerfing this device, and preventing an new API
    // operations on it.  We need to be inside the dev lock to do this.  Note:
    // it is assumed that we will not disappear during any of this function,
    // because our caller is holding a reference to us.
    fbl::AutoLock dev_lock(&dev_lock_);
    // Disable should have been called before Unplug and would have disabled
    // everything in the command register
    ZX_DEBUG_ASSERT(disabled_);
    upstream_->UnlinkDevice(this);
    bli_->UnlinkDevice(this);
    plugged_in_ = false;
    pci_tracef("device [%s] unplugged\n", cfg_->addr());
}

void Device::Dump() const {
    pci_infof("%s at %s vid:did %04x:%04x\n", (is_bridge()) ? "bridge" : "device", cfg_->addr(),
              vendor_id(), device_id());
    for (size_t i = 0; i < bar_count_; i++) {
        auto& bar = bars_[i];
        if (bar.size) {
            pci_infof("    bar %zu: %s, %s, addr %#lx, size %#zx [raw: ", i,
                      (bar.is_mmio) ? ((bar.is_64bit) ? "64bit mmio" : "32bit mmio") : "io",
                      (bar.is_prefetchable) ? "pf" : "no-pf", bar.address, bar.size);
            if (bar.is_64bit) {
                zxlogf(INFO, "%08x ", cfg_->Read(Config::kBar(bar.bar_id + 1)));
            }
            zxlogf(INFO, "%08x ]\n", cfg_->Read(Config::kBar(bar.bar_id)));
        }
    }
    if (!caps_.list.is_empty()) {
        pci_infof("    capabilities: ");
        for (auto& cap : caps_.list) {
            auto id = static_cast<Capability::Id>(cap.id());
            zxlogf(INFO, "%s (%#x)%s", CapabilityIdToName(id), cap.id(),
                   (&cap == &caps_.list.back()) ? "\n" : ", ");
        }
    }
}

} // namespace pci
