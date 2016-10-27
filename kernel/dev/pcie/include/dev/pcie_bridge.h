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

class PcieBridge : public PcieDevice {
public:
    static mxtl::RefPtr<PcieDevice> Create(PcieBridge& upstream,
                                           uint dev_id,
                                           uint func_id,
                                           uint managed_bus_id);
    static mxtl::RefPtr<PcieBridge> CreateRoot(PcieBusDriver& bus_drv, uint managed_bus_id);
    virtual ~PcieBridge();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBridge);

    mxtl::RefPtr<PcieDevice> GetDownstream(uint ndx) { return bus_drv_.GetDownstream(*this, ndx); }

    void Unplug() override;

    // TODO(johngro) : bury these once we refactor roots.  It should not need to be public.
    void ScanDownstream();
    void AllocateDownstreamBars();

    uint64_t pf_mem_base()        const { return pf_mem_base_; }
    uint64_t pf_mem_limit()       const { return pf_mem_limit_; }
    uint32_t mem_base()           const { return mem_base_; }
    uint32_t mem_limit()          const { return mem_limit_; }
    uint32_t io_base()            const { return io_base_; }
    uint32_t io_limit()           const { return io_limit_; }
    bool     supports_32bit_pio() const { return supports_32bit_pio_; }
    uint     managed_bus_id()     const { return managed_bus_id_; }

    // TODO(johngro) : hide these once we refactor roots.  Currently, PcieDevice
    // needs access to them in order to allocate BARs.
    RegionAllocator mmio_lo_regions_;
    RegionAllocator mmio_hi_regions_;
    RegionAllocator pio_regions_;

private:
    friend class PcieBusDriver;

    RegionAllocator::Region::UPtr mmio_window_;
    RegionAllocator::Region::UPtr pio_window_;

    const uint managed_bus_id_;  // The ID of the downstream bus which this bridge manages.

    uint64_t pf_mem_base_;
    uint64_t pf_mem_limit_;
    uint32_t mem_base_;
    uint32_t mem_limit_;
    uint32_t io_base_;
    uint32_t io_limit_;
    bool     supports_32bit_pio_;

    /* An array of pointers for all the possible functions which exist on the
     * downstream bus of this bridge. */
    mxtl::RefPtr<PcieDevice> downstream_[PCIE_MAX_FUNCTIONS_PER_BUS];

    mxtl::RefPtr<PcieDevice> ScanDevice(pcie_config_t* cfg, uint dev_id, uint func_id);
    status_t ParseBusWindowsLocked();

    status_t Init(PcieBridge& upstream);
    status_t AllocateBarsLocked(PcieBridge& upstream) override;
    void     DisableLocked() override;

    PcieBridge(PcieBusDriver& bus_drv, uint bus_id, uint dev_id, uint func_id, uint mbus_id);
    PcieBridge(PcieBusDriver& bus_drv, uint mbus_id);
};
