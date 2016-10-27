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

pcie_bridge_state_t::pcie_bridge_state_t(PcieBusDriver& bus_driver, uint mbus_id)
    : pcie_device_state_t(bus_driver),
      managed_bus_id(mbus_id) {
    is_bridge = true;

    /* Assign the driver-wide region pool to this bridge's allocators. */
    DEBUG_ASSERT(bus_drv.region_bookkeeping() != nullptr);
    mmio_lo_regions.SetRegionPool(bus_drv.region_bookkeeping());
    mmio_hi_regions.SetRegionPool(bus_drv.region_bookkeeping());
    pio_regions.SetRegionPool(bus_drv.region_bookkeeping());
}

pcie_bridge_state_t::~pcie_bridge_state_t() {
#if LK_DEBUGLEVEL > 0
     /* Sanity check to make sure that all child devices have been released as well. */
    for (size_t i = 0; i < countof(downstream); ++i)
        DEBUG_ASSERT(!downstream[i]);
#endif
}

void pcie_bridge_parse_windows(const mxtl::RefPtr<pcie_bridge_state_t>& bridge) {
    DEBUG_ASSERT(bridge);

    /* Parse the currently configured windows used to determine MMIO/PIO
     * forwarding policy for this bridge.
     *
     * See The PCI-to-PCI Bridge Architecture Specification Revision 1.2,
     * section 3.2.5 and chapter 4 for detail.. */
    auto& bcfg = *(reinterpret_cast<pci_to_pci_bridge_config_t*>(&bridge->cfg->base));
    uint32_t base, limit;

    // I/O window
    base  = pcie_read8(&bcfg.io_base);
    limit = pcie_read8(&bcfg.io_limit);

    bridge->supports_32bit_pio = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit& 0xF));
    bridge->io_base  = (base & ~0xF) << 8;
    bridge->io_limit = (limit << 8) | 0xFFF;
    if (bridge->supports_32bit_pio) {
        bridge->io_base  |= static_cast<uint32_t>(pcie_read16(&bcfg.io_base_upper)) << 16;
        bridge->io_limit |= static_cast<uint32_t>(pcie_read16(&bcfg.io_limit_upper)) << 16;
    }

    bridge->io_base  = base;
    bridge->io_limit = limit;

    // Non-prefetchable memory window
    bridge->mem_base  = (static_cast<uint32_t>(pcie_read16(&bcfg.memory_base)) << 16)
                      & ~0xFFFFF;
    bridge->mem_limit = (static_cast<uint32_t>(pcie_read16(&bcfg.memory_limit)) << 16)
                      | 0xFFFFF;

    // Prefetchable memory window
    base  = pcie_read16(&bcfg.prefetchable_memory_base);
    limit = pcie_read16(&bcfg.prefetchable_memory_limit);

    bool supports_64bit_pf_mem = ((base & 0xF) == 0x1) && ((base & 0xF) == (limit& 0xF));
    bridge->pf_mem_base  = (base & ~0xF) << 16;;
    bridge->pf_mem_limit = (limit << 16) | 0xFFFFF;
    if (supports_64bit_pf_mem) {
        bridge->pf_mem_base  |=
            static_cast<uint64_t>(pcie_read32(&bcfg.prefetchable_memory_base_upper)) << 32;
        bridge->pf_mem_limit |=
            static_cast<uint64_t>(pcie_read32(&bcfg.prefetchable_memory_limit_upper)) << 32;
    }
}

void pcie_scan_bus(const mxtl::RefPtr<pcie_bridge_state_t>& bridge) {
    DEBUG_ASSERT(bridge);

    for (uint dev_id = 0; dev_id < PCIE_MAX_DEVICES_PER_BUS; ++dev_id) {
        for (uint func_id = 0; func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++func_id) {
            /* If we can find the config, and it has a valid vendor ID, go ahead
             * and scan it looking for a valid function. */
            pcie_config_t* cfg = bridge->bus_drv.GetConfig(bridge->managed_bus_id,
                                                           dev_id,
                                                           func_id);
            bool good_device = cfg && (pcie_read16(&cfg->base.vendor_id) != PCIE_INVALID_VENDOR_ID);
            if (good_device) {
                /* Don't scan the function again if we have already discovered
                 * it.  If this function happens to be a bridge, go ahead and
                 * look under it for new devices. */
                uint ndx    = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
                DEBUG_ASSERT(ndx < countof(bridge->downstream));

                auto downstream = bridge->GetDownstream(ndx);
                if (!downstream) {
                    pcie_scan_function(bridge, cfg, dev_id, func_id);
                } else {
                    auto downstream_bridge = downstream->DowncastToBridge();
                    if (downstream_bridge)
                        pcie_scan_bus(downstream_bridge);
                }
            }

            /* If this was function zero, and there is either no device, or the
             * config's header type indicates that this is not a multi-function
             * device, then just move on to the next device. */
            if (!func_id &&
               (!good_device || !(pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MULTI_FN)))
                break;
        }
    }
}

void pcie_bridge_state_t::Unplug() {
    pcie_device_state_t::Unplug();

    for (uint i = 0; i < countof(downstream); ++i) {
        auto downstream_device = GetDownstream(i);
        if (downstream_device)
            downstream_device->Unplug();
    }
}

void pcie_allocate_downstream_bars(const mxtl::RefPtr<pcie_bridge_state_t>& bridge) {
    DEBUG_ASSERT(bridge != nullptr);

    for (size_t i = 0; i < countof(bridge->downstream); ++i) {
        if (bridge->downstream[i]) {
            pcie_allocate_bars(bridge->downstream[i]);
        }
    }
}
