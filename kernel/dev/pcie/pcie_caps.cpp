// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <string.h>
#include <trace.h>
#include <fbl/algorithm.h>
#include <dev/pci_config.h>
#include <dev/pcie_device.h>

#include <fbl/alloc_checker.h>

#define LOCAL_TRACE 0

/*
 * TODO(cja) Re-add the paranoid sanity checks on capability placement
 * and size that was in the old code. Doing this sanely likely involves keeping
 * the various C style structures for the capabilities in pcie_caps.h originally
*/

static bool quirk_should_force_pcie(const PcieDevice& dev) {
    static const struct {
        uint16_t vendor_id;
        uint16_t device_id;
    } QUIRK_LIST[] = {
        { .vendor_id = 0x8086, .device_id = 0x1616 },  // Wildcat Point GPU
    };

    for (size_t i = 0; i < fbl::count_of(QUIRK_LIST); ++i) {
        if ((QUIRK_LIST[i].vendor_id == dev.vendor_id()) &&
            (QUIRK_LIST[i].device_id == dev.device_id()))
            return true;
    }

    return false;
}


/*
 * Advanced Capabilities for Conventional PCI ECN
 */
PciCapAdvFeatures::PciCapAdvFeatures(const PcieDevice& dev, uint16_t base, uint8_t id)
    : PciStdCapability(dev, base, id) {
    DEBUG_ASSERT(id == PCIE_CAP_ID_ADVANCED_FEATURES);
    auto cfg = dev.config();

    length_    = PciReg8(static_cast<uint16_t>(base_ + kLengthOffset));
    af_caps_   = PciReg8(static_cast<uint16_t>(base_ + kAFCapsOffset));
    af_ctrl_   = PciReg8(static_cast<uint16_t>(base_ + kAFControlOffset));
    af_status_ = PciReg8(static_cast<uint16_t>(base_ + kAFStatusOffset));

    uint8_t caps = cfg->Read(af_caps_);
    has_flr_ = PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(caps);
    has_tp_  = PCS_ADVCAPS_CAP_HAS_TRANS_PENDING(caps);

    uint8_t length = cfg->Read(length_);
    if (length != PCS_ADVCAPS_LENGTH) {
        TRACEF("Length of %u does not match the spec length of %u!\n", length, PCS_ADVCAPS_LENGTH);
        return;
    }

    is_valid_ = true;
}

/*
 * PCI Express Base Specification 1.1  Section 7.8 (version 1)
 * PCI Express Base Specification 3.1a Section 7.8 (version 2)
 */
PciCapPcie::PciCapPcie(const PcieDevice& dev, uint16_t base, uint8_t id)
    : PciStdCapability(dev, base, id) {
    DEBUG_ASSERT(id == PCIE_CAP_ID_PCI_EXPRESS);
    auto cfg = dev.config();

    /* Have we already initialized PCIE? */
    if (is_valid_) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has more than one PCI "
                "Express capability structure!\n",
               dev.bus_id(), dev.dev_id(), dev.func_id(),
               dev.vendor_id(), dev.device_id());
        return;
    }

    caps_ = PciReg16(static_cast<uint16_t>(base_ + kPcieCapsOffset));
    auto cap_val = cfg->Read(caps_);
    version_ = PCS_CAPS_VERSION(cap_val);
    devtype_ = PCS_CAPS_DEVTYPE(cap_val);

    /*
     * Set up all the offsets for the various chunks in the device. Some may
     * not be supported, but regardless of whether they are there the final
     * structure will be the same.
     */
    device.caps_    = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kDeviceOffset)));
    device.ctrl_    = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kDeviceOffset)));
    device.status_  = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kDeviceOffset)));

    link.caps_      = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kLinkOffset)));
    link.ctrl_      = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kLinkOffset)));
    link.status_    = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kLinkOffset)));

    slot.caps_      = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kSlotOffset)));
    slot.ctrl_      = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kSlotOffset)));
    slot.status_    = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kSlotOffset)));

    root.caps_      = PciReg16(static_cast<uint16_t>(base_ + kRootCapsOffset));
    root.ctrl_      = PciReg16(static_cast<uint16_t>(base_ + kRootControlOffset));
    root.status_    = PciReg32(static_cast<uint16_t>(base_ + kRootStatusOffset));

    device2.caps_   = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kDevice2Offset)));
    device2.ctrl_   = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kDevice2Offset)));
    device2.status_ = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kDevice2Offset)));

    link2.caps_     = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kLinkOffset)));
    link2.ctrl_     = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kLinkOffset)));
    link2.status_   = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kLinkOffset)));

    slot2.caps_     = PciReg32(static_cast<uint16_t>(base_ + kCapsOffset(kSlotOffset)));
    slot2.ctrl_     = PciReg16(static_cast<uint16_t>(base_ + kControlOffset(kSlotOffset)));
    slot2.status_   = PciReg16(static_cast<uint16_t>(base_ + kStatusOffset(kSlotOffset)));

    /* Sanity check the device/port type */
    switch (devtype_) {
        // Type 0 config header types
        case PCIE_DEVTYPE_PCIE_ENDPOINT:
        case PCIE_DEVTYPE_LEGACY_PCIE_ENDPOINT:
        case PCIE_DEVTYPE_RC_INTEGRATED_ENDPOINT:
        case PCIE_DEVTYPE_RC_EVENT_COLLECTOR:
            if (dev.is_bridge()) {
                TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has a Type 0 PCIe "
                       "device type (0x%x) in PCIe capabilties structure, but "
                       "does not have a Type 0 config header.\n",
                       dev.bus_id(), dev.dev_id(), dev.func_id(),
                       dev.vendor_id(), dev.device_id(),
                       devtype_);
                return;
            }
            break;

        // Type 1 config header types
        case PCIE_DEVTYPE_RC_ROOT_PORT:
        case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:
        case PCIE_DEVTYPE_SWITCH_DOWNSTREAM_PORT:
        case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:
        case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:
            if (!dev.is_bridge()) {
                TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has a Type 1 PCIe "
                       "device type (0x%x) in PCIe capabilties structure, but "
                       "does not have a Type 1 config header.\n",
                       dev.bus_id(), dev.dev_id(), dev.func_id(),
                       dev.vendor_id(), dev.device_id(),
                       devtype_);
                return;
            }
            break;

        default:
            TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has an illegal PCIe "
                   "device type (0x%x) in PCIe capabilties structure.\n",
                   dev.bus_id(), dev.dev_id(), dev.func_id(),
                   dev.vendor_id(), dev.device_id(),
                   devtype_);
            return;
    }

    /* TODO(johngro): remember to read the MSI/MSI-X interrupt message number
     * field when setting up for MSI/MSI-X.  We almost certainly need to hook
     * this IRQ in order to be aware of any changes to the extended
     * capabilities.  It is unclear whether or not we should allow this IRQ to
     * be passed thru to the device driver or not.
     */

    /* Check device capabilities to see if we support function level reset or
     * not */
    uint32_t devcaps = cfg->Read(device.caps());
    has_flr_ = (PCS_DEV_CAPS_FUNC_LEVEL_RESET(devcaps) != 0);

    is_valid_ = true;
}

/*
 * @see PCI Local Bus Specification 3.0 Section 6.8.1
 */
PciCapMsi::PciCapMsi(const PcieDevice& dev, uint16_t base, uint8_t id)
    : PciStdCapability(dev, base, id) {
    DEBUG_ASSERT(id == PCIE_CAP_ID_MSI);
    auto cfg = dev.config();

    // Set up the rest of the registers based on whether we're 64 bit or not.
    ctrl_     = PciReg16(static_cast<uint16_t>(base_ + kControlOffset));
    addr_     = PciReg32(static_cast<uint16_t>(base_ + kAddrOffset));

    uint16_t ctrl = cfg->Read(ctrl_reg());
    has_pvm_      = PCIE_CAP_MSI_CTRL_PVM_SUPPORTED(ctrl);
    is_64_bit_    = PCIE_CAP_MSI_CTRL_64BIT_SUPPORTED(ctrl);
    msi_size_     = (has_pvm_ ? (is_64_bit_ ? k64BitPvmSize : k32BitPvmSize)
                              : (is_64_bit_ ? k64BitNoPvmSize : k32BitNoPvmSize));

    if (is_64_bit_) {
        addr_upper_   = PciReg32(static_cast<uint16_t>(base_ + kAddrUpperOffset));
        data_         = PciReg16(static_cast<uint16_t>(base_ + kData64Offset));
        mask_bits_    = PciReg32(static_cast<uint16_t>(base_ + kMaskBits64Offset));
        pending_bits_ = PciReg32(static_cast<uint16_t>(base_ + kPendingBits64Offset));
    } else {
        data_         = PciReg16(static_cast<uint16_t>(base_ + kData32Offset));
        mask_bits_    = PciReg32(static_cast<uint16_t>(base_ + kMaskBits32Offset));
        pending_bits_ = PciReg32(static_cast<uint16_t>(base_ + kPendingBits32Offset));
    }

    memset(&irq_block_, 0, sizeof(irq_block_));
    uint16_t msi_end = static_cast<uint16_t>(base_ + msi_size_);
    uint16_t cfgend = PCIE_BASE_CONFIG_SIZE;

    if (msi_end >= cfgend) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegally positioned MSI "
               "capability structure.  Structure %s 64-bit addressing and %s "
               "per-vector masking and should be %u bytes long, but the "
               "structure ends at %u, %u bytes past the end of config "
               "space\n",
               dev.bus_id(), dev.dev_id(), dev.func_id(),
               dev.vendor_id(), dev.device_id(),
               is_64_bit_ ? "supports" : "does not support",
               has_pvm_ ? "supports" : "does not support",
               msi_size_, msi_end, static_cast<unsigned int>(cfgend - msi_end));
        return;
    }

    /* Sanity check the Multi-Message Capable field */
    max_irqs_ = 0x1u << PCIE_CAP_MSI_CTRL_GET_MMC(ctrl);
    if (max_irqs_ > PCIE_MAX_MSI_IRQS) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has invalid Multi-Message "
               "Capable value in MSI capability structure (%d).  Structure "
               "claims to support %u vectors, but %u is the maximum allowed.\n",
               dev.bus_id(), dev.dev_id(), dev.func_id(),
               dev.vendor_id(), dev.device_id(),
               PCIE_CAP_MSI_CTRL_GET_MMC(ctrl), max_irqs_, PCIE_MAX_MSI_IRQS);
        return;
    }

    /* Success!
     *
     * Make sure that MSI is disabled and that the Multi-Message Enable field is
     * set to 1-vector (multi-message disabled).  Then record our capabilities
     * in the device's bookkeeping and we are done.
     */
    cfg->Write(ctrl_reg(), PCIE_CAP_MSI_CTRL_SET_MME(0,
                           PCIE_CAP_MSI_CTRL_SET_ENB(0, ctrl)));
    if (has_pvm_)
        cfg->Write(mask_bits_reg(), 0xFFFFFFFF);

    is_valid_ = true;
}

/* Catch quirks and invalid capability offsets we may see */
inline status_t validate_capability_offset(uint8_t offset) {
    if (offset == 0xFF
        || offset < PCIE_CAP_PTR_MIN_VALID
        || offset > PCIE_CAP_PTR_MAX_VALID) {
        return MX_ERR_INVALID_ARGS;
    }

    return MX_OK;
}

/*
 * TODO(cja): It may be worth moving to table based solution like we had before
 * where we have a single parse function and a function table for it to use,
 * but it involves a bit more worrying about ownership of capabilities and
 * std / ext attributes.
 */

status_t PcieDevice::ParseStdCapabilitiesLocked() {
    status_t res = MX_OK;
    uint8_t cap_offset = cfg_->Read(PciConfig::kCapabilitiesPtr);
    uint8_t caps_found = 0;
    fbl::AllocChecker ac;

    /*
     * Walk the pointer list for the standard capabilities table. As a safety,
     * keep track of how many capabilities we've looked at to prevent potential
     * cycles from walking forever. Any supported capability will be parsed
     * by their object in the PcieDevice, and are additionally stored in a list
     * for reference later.
     */
    LTRACEF("Scanning for capabilities at %02x:%02x.%01x (%04hx:%04hx)\n",
            bus_id(), dev_id(), func_id(), vendor_id(), device_id());
    while (cap_offset != PCIE_CAP_PTR_NULL && caps_found < PCIE_MAX_CAPABILITIES) {
        if ((res = validate_capability_offset(cap_offset)) != MX_OK) {
            TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has invalid cptr (%#02x)\n",
                    bus_id(), dev_id(), func_id(),
                    vendor_id(), device_id(), cap_offset);
            break;
        }

        uint8_t id = cfg_->Read(PciReg8(cap_offset));

        LTRACEF("Found capability (#%u, id = 0x%02x) for device %02x:%02x.%01x (%04hx:%04hx)\n",
                caps_found, id,
                bus_id(), dev_id(), func_id(),
                vendor_id(), device_id());
        /*
         * Depending on the capability found we allocate a structure of the appropriate type
         * and add it to the bookkeeping tree. For important things like MSI/PCIE we cache a raw
         * pointer to it for fast access, but otherwise everything is found via the capability list.
         *
         * TODO(cja): if we make this a two stage allocation/initialization in the future we can do
         * away with is_valid() style checks that are done in additional checking if pcie_/msi_ are
         * valid pointers.
         */
        PciStdCapability* cap;
        switch(id) {
            case PCIE_CAP_ID_MSI:
                cap = irq_.msi = new (&ac) PciCapMsi(*this, cap_offset, id); break;
            case PCIE_CAP_ID_PCI_EXPRESS:
                cap = pcie_ = new (&ac) PciCapPcie(*this, cap_offset, id); break;
            case PCIE_CAP_ID_ADVANCED_FEATURES:
                cap = pci_af_ = new (&ac) PciCapAdvFeatures(*this, cap_offset, id); break;

            default:
                cap = new (&ac) PciStdCapability(*this, cap_offset, id); break;
        }

        if (!ac.check()) {
            TRACEF("Could not allocate memory fori capability 0x%02x\n", id);
            return MX_ERR_NO_MEMORY;
        }

        caps_.detected.push_front(fbl::unique_ptr<PciStdCapability>(cap));
        cap_offset  = cfg_->Read(PciReg8(static_cast<uint16_t>(cap_offset + 0x1))) & 0xFC;
        caps_found++;
    }

    return res;
}

status_t PcieDevice::ParseExtCapabilitiesLocked() {
    /*
     * TODO(cja): Since ExtCaps are a no-op right now (we had nothing in the table for
     * supported extended capabilities) this is a stub for now.
     */
    return MX_OK;
}

// Parse PCI Standard Capabilities starting with the pointer in the PCI
// config structure.
status_t PcieDevice::ProbeCapabilitiesLocked() {
    status_t ret = ParseStdCapabilitiesLocked();
    if (ret != MX_OK) {
        return ret;
    }

    /* If this device is PCIe device, the parse the extended configuration
     * section of the PCI config looking for extended capabilities.  Based on
     * the spec, we should only need to look for a PCI Express Capability
     * Structure in the standard config section to make the determination that
     * this device is a legit PCIe device.
     *
     * This said, I have encountered at least one device (the graphics
     * controller in the Wildcat Point PCH) which clearly is PCIe and clearly
     * has extended capabilities, but which is not spec compliant and does not
     * contain a proper PCI Express Capability Structure.  Because of this, we
     * maintain a quirks list of non compliant devices which are actually PCIe,
     * but do not appear to be so at first glance. */
    if (pcie_->is_valid() || quirk_should_force_pcie(*this)) {
        ret = ParseExtCapabilitiesLocked();
    }

    return ret;
}
