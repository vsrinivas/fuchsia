// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <magenta/errors.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_constants.h>
#include <dev/pcie_device.h>
#include <kernel/mutex.h>
#include <region-alloc/region-alloc.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

/* Fwd decls */
class PcieBusDriver;

struct pcie_bridge_state_t : public pcie_device_state_t {
    pcie_bridge_state_t(PcieBusDriver& bus_driver, uint mbus_id);
    virtual ~pcie_bridge_state_t();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(pcie_bridge_state_t);

    mxtl::RefPtr<pcie_device_state_t> GetDownstream(uint ndx) {
        return bus_drv.GetDownstream(*this, ndx);
    }

    void Unplug() override;

    const uint managed_bus_id;  // The ID of the downstream bus which this bridge manages.

    RegionAllocator mmio_lo_regions;
    RegionAllocator mmio_hi_regions;
    RegionAllocator pio_regions;

    RegionAllocator::Region::UPtr mmio_window;
    RegionAllocator::Region::UPtr pio_window;

    uint64_t pf_mem_base;
    uint64_t pf_mem_limit;
    uint32_t mem_base;
    uint32_t mem_limit;
    uint32_t io_base;
    uint32_t io_limit;
    bool     supports_32bit_pio;

    /* An array of pointers for all the possible functions which exist on the
     * downstream bus of this bridge.  Note: in the special case of the root
     * host bridge, the function pointer will always be NULL in order to avoid
     * cycles in the graph.
     */
    mxtl::RefPtr<pcie_device_state_t> downstream[PCIE_MAX_FUNCTIONS_PER_BUS];
};

void pcie_bridge_parse_windows(const mxtl::RefPtr<pcie_bridge_state_t>& bridge);
