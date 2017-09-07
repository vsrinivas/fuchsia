// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <lk/init.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include <dev/pcie_bridge.h>
#include <dev/pcie_root.h>

#define LOCAL_TRACE 0

PcieUpstreamNode::~PcieUpstreamNode() {
#if LK_DEBUGLEVEL > 0
     // Sanity check to make sure that all child devices have been released as
     // well.
    for (size_t i = 0; i < fbl::count_of(downstream_); ++i)
        DEBUG_ASSERT(!downstream_[i]);
#endif
}

void PcieUpstreamNode::AllocateDownstreamBars() {
    /* Finally, allocate all of the BARs for our downstream devices.  Make sure
     * to not access our downstream devices directly.  Instead, hold references
     * to downstream devices we obtain while holding bus driver's topology lock.
     * */
    for (uint i = 0; i < fbl::count_of(downstream_); ++i) {
        auto device = GetDownstream(i);
        if (device != nullptr) {
            status_t res = device->AllocateBars();
            if (res != MX_OK)
                device->Disable();
        }
    }
}

void PcieUpstreamNode::DisableDownstream() {
    for (uint i = 0; i < fbl::count_of(downstream_); ++i) {
        auto downstream_device = GetDownstream(i);
        if (downstream_device)
            downstream_device->Disable();
    }
}

void PcieUpstreamNode::UnplugDownstream() {
    for (uint i = 0; i < fbl::count_of(downstream_); ++i) {
        auto downstream_device = GetDownstream(i);
        if (downstream_device)
            downstream_device->Unplug();
    }
}

void PcieUpstreamNode::ScanDownstream() {
    DEBUG_ASSERT(driver().RescanLockIsHeld());

    for (uint dev_id = 0; dev_id < PCIE_MAX_DEVICES_PER_BUS; ++dev_id) {
        for (uint func_id = 0; func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++func_id) {
            /* If we can find the config, and it has a valid vendor ID, go ahead
             * and scan it looking for a valid function. */
            auto cfg = driver().GetConfig(managed_bus_id_, dev_id, func_id);
            if (cfg == nullptr) {
                TRACEF("Warning: bus being scanned is outside ecam region!\n");
                return;
            }

            uint16_t vendor_id = cfg->Read(PciConfig::kVendorId);
            bool good_device = cfg && (vendor_id != PCIE_INVALID_VENDOR_ID);
            if (good_device) {
                uint16_t device_id = cfg->Read(PciConfig::kDeviceId);
                LTRACEF("found valid device %04x:%04x at %02x:%02x.%01x\n",
                        vendor_id, device_id, managed_bus_id_, dev_id, func_id);
                /* Don't scan the function again if we have already discovered
                 * it.  If this function happens to be a bridge, go ahead and
                 * look under it for new devices. */
                uint ndx    = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
                DEBUG_ASSERT(ndx < fbl::count_of(downstream_));

                auto downstream_device = GetDownstream(ndx);
                if (!downstream_device) {
                    auto new_dev = ScanDevice(cfg, dev_id, func_id);
                    if (new_dev == nullptr) {
                        TRACEF("Failed to initialize device %02x:%02x.%01x; This is Very Bad.  "
                               "Device (and any of its children) will be inaccessible!\n",
                               managed_bus_id_, dev_id, func_id);
                        good_device = false;
                    }
                } else if (downstream_device->is_bridge()) {
                    // TODO(johngro) : Instead of going up and down the class graph with static
                    // casts, would it be better to do this with vtable tricks?
                    static_cast<PcieUpstreamNode*>(
                    static_cast<PcieBridge*>(downstream_device.get()))->ScanDownstream();
                }
            }

            /* If this was function zero, and there is either no device, or the
             * config's header type indicates that this is not a multi-function
             * device, then just move on to the next device. */
            if (!func_id &&
               (!good_device || !(cfg->Read(PciConfig::kHeaderType) & PCI_HEADER_TYPE_MULTI_FN)))
                break;
        }
    }
}

fbl::RefPtr<PcieDevice> PcieUpstreamNode::ScanDevice(const PciConfig* cfg,
                                                      uint dev_id,
                                                      uint func_id) {
    DEBUG_ASSERT(cfg);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);
    DEBUG_ASSERT(driver().RescanLockIsHeld());

    __UNUSED uint ndx = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
    DEBUG_ASSERT(ndx < fbl::count_of(downstream_));
    DEBUG_ASSERT(downstream_[ndx] == nullptr);

    LTRACEF("Scanning new function at %02x:%02x.%01x\n", managed_bus_id_, dev_id, func_id);

    /* Is there an actual device here? */
    uint16_t vendor_id = cfg->Read(PciConfig::kVendorId);
    if (vendor_id == PCIE_INVALID_VENDOR_ID) {
        LTRACEF("Bad vendor ID (0x%04hx) when looking for PCIe device at %02x:%02x.%01x\n",
                vendor_id, managed_bus_id_, dev_id, func_id);
        return nullptr;
    }

    // Create the either a PcieBridge or a PcieDevice based on the configuration
    // header type.
    uint8_t header_type = cfg->Read(PciConfig::kHeaderType) & PCI_HEADER_TYPE_MASK;
    if (header_type == PCI_HEADER_TYPE_PCI_BRIDGE) {
        uint secondary_id = cfg->Read(PciConfig::kSecondaryBusId);
        return PcieBridge::Create(*this, dev_id, func_id, secondary_id);
    }

    return PcieDevice::Create(*this, dev_id, func_id);
}
