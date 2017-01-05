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
#include <dev/pcie_device.h>
#include <dev/pcie_ref_counted.h>
#include <dev/pcie_upstream_node.h>

#include <region-alloc/region-alloc.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

class PciConfig;
class PcieBridge : public PcieDevice,
                   public PcieUpstreamNode {
public:
    static mxtl::RefPtr<PcieDevice> Create(PcieUpstreamNode& upstream,
                                           uint dev_id,
                                           uint func_id,
                                           uint managed_bus_id);

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieBridge);

    // Implement ref counting, do not let derived classes override.
    PCIE_IMPLEMENT_REFCOUNTED;

    // Device overrides
    void Unplug() override;

    // UpstreamNode overrides
    RegionAllocator& mmio_lo_regions() final { return mmio_lo_regions_; }
    RegionAllocator& mmio_hi_regions() final { return mmio_hi_regions_; }
    RegionAllocator& pio_regions()     final { return pio_regions_; }

    // Properties
    PcieBusDriver& driver() { return PcieDevice::driver(); }

    uint64_t pf_mem_base()        const { return pf_mem_base_; }
    uint64_t pf_mem_limit()       const { return pf_mem_limit_; }
    uint32_t mem_base()           const { return mem_base_; }
    uint32_t mem_limit()          const { return mem_limit_; }
    uint32_t io_base()            const { return io_base_; }
    uint32_t io_limit()           const { return io_limit_; }
    bool     supports_32bit_pio() const { return supports_32bit_pio_; }

protected:
    status_t AllocateBars() override;
    status_t AllocateBridgeWindowsLocked();
    void     Disable() override;

private:
    friend class PcieBusDriver;

    PcieBridge(PcieBusDriver& bus_drv, uint bus_id, uint dev_id, uint func_id, uint mbus_id);

    status_t ParseBusWindowsLocked();
    status_t Init(PcieUpstreamNode& upstream);

    RegionAllocator mmio_lo_regions_;
    RegionAllocator mmio_hi_regions_;
    RegionAllocator pio_regions_;

    RegionAllocator::Region::UPtr mmio_window_;
    RegionAllocator::Region::UPtr pio_window_;

    uint64_t pf_mem_base_;
    uint64_t pf_mem_limit_;
    uint32_t mem_base_;
    uint32_t mem_limit_;
    uint32_t io_base_;
    uint32_t io_limit_;
    bool     supports_32bit_pio_;
    mxtl::RefPtr<PcieDevice> ScanDevice(const PciConfig* cfg, uint dev_id, uint func_id);
};
