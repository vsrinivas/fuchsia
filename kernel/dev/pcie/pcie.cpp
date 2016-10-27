// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <dev/pcie.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <new.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include "pcie_priv.h"

#define LOCAL_TRACE 0

void pcie_scan_function(const mxtl::RefPtr<pcie_bridge_state_t>& upstream_bridge,
                        pcie_config_t*                           cfg,
                        uint                                     dev_id,
                        uint                                     func_id) {
    DEBUG_ASSERT(upstream_bridge && cfg);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    mxtl::RefPtr<pcie_device_state_t> dev;
    uint bus_id = upstream_bridge->managed_bus_id;
    __UNUSED uint ndx = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;

    DEBUG_ASSERT(ndx < countof(upstream_bridge->downstream));
    DEBUG_ASSERT(upstream_bridge->downstream[ndx] == nullptr);

    /* Is there an actual device here? */
    uint16_t vendor_id = pcie_read16(&cfg->base.vendor_id);
    if (vendor_id == PCIE_INVALID_VENDOR_ID)
        return;

    LTRACEF("Scanning new function at %02x:%02x.%01x\n", bus_id, dev_id, func_id);

    /* If this function is a PCI bridge, extract the bus ID of the other side of
     * the bridge, initialize the bridge node and recurse.
     *
     * TODO(johngro) : Add some protection against cycles in the bridge
     * configuration which could lead to infinite recursion.
     */
    uint8_t header_type = pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MASK;
    if (header_type == PCI_HEADER_TYPE_PCI_BRIDGE) {
        pci_to_pci_bridge_config_t* bridge_cfg = (pci_to_pci_bridge_config_t*)(&cfg->base);

        uint primary_id   = pcie_read8(&bridge_cfg->primary_bus_id);
        uint secondary_id = pcie_read8(&bridge_cfg->secondary_bus_id);

        if (primary_id != bus_id) {
            TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x has invalid primary bus id "
                   "(%02x)... skipping scan.\n",
                   bus_id, dev_id, func_id, primary_id);
            return;
        }

        if (primary_id == secondary_id) {
            TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x claims to be bridged to itsef "
                   "(primary %02x == secondary %02x)... skipping scan.\n",
                   bus_id, dev_id, func_id, primary_id, secondary_id);
            return;
        }

        /* Allocate and initialize our bridge structure */
        AllocChecker ac;
        auto bridge = mxtl::AdoptRef(new (&ac) pcie_bridge_state_t(upstream_bridge->bus_drv,
                                                                   secondary_id));
        if (!ac.check()) {
            DEBUG_ASSERT(!bridge);
            TRACEF("Failed to allocate bridge node for %02x:%02x.%01x during bus scan.\n",
                    bus_id, dev_id, func_id);
            return;
        }

        dev = pcie_upcast_to_device(mxtl::move(bridge));
    } else {
        /* Allocate and initialize our device structure */

        AllocChecker ac;
        dev = mxtl::AdoptRef(new (&ac) pcie_device_state_t(upstream_bridge->bus_drv));
        if (!ac.check()) {
            DEBUG_ASSERT(!dev);
            TRACEF("Failed to allocate device node for %02x:%02x.%01x during bus scan.\n",
                    bus_id, dev_id, func_id);
            return;
        }
    }

    /* Initialize common fields, linking up the graph in the process. */
    status_t res = pcie_scan_init_device(dev,
                                         upstream_bridge,
                                         bus_id, dev_id, func_id);
    if (NO_ERROR == res) {
        /* If this was a bridge device, recurse and continue probing. */
        auto bridge = dev->DowncastToBridge();
        if (bridge)
            pcie_scan_bus(bridge);
    } else {
        /* Something went terribly wrong during init.  ASSERT that we are not
         * tracking this device upstream, and release it.  No need to log,
         * pcie_scan_init_device has done so already for us.
         */
        DEBUG_ASSERT(upstream_bridge->downstream[ndx] == nullptr);
        dev = nullptr;
    }
}

/*
 * For iterating through all PCI devices. Returns the nth device, or NULL
 * if index is >= the number of PCI devices.
 */
mxtl::RefPtr<pcie_device_state_t> pcie_get_nth_device(uint32_t index) {
    auto driver = PcieBusDriver::GetDriver();
    if (!driver)
        return nullptr;

    return driver->GetNthDevice(index);
}

void pcie_shutdown(void) {
    PcieBusDriver::ShutdownDriver();
}

void pcie_rescan_bus(void) {
    auto driver = PcieBusDriver::GetDriver();
    if (driver)
        driver->ScanDevices();
}
