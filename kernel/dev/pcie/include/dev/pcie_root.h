// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie_bus_driver.h>
#include <dev/pcie_upstream_node.h>
#include <magenta/compiler.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/ref_counted.h>

class PcieRoot : public mxtl::RefCounted<PcieRoot>,
                 public PcieUpstreamNode {
public:
    static mxtl::RefPtr<PcieRoot> Create(PcieBusDriver& bus_drv, uint managed_bus_id);

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieRoot);

    PcieBusDriver& driver() { return bus_drv_; }
    RegionAllocator& mmio_lo_regions() override { return bus_drv_.mmio_lo_regions(); }
    RegionAllocator& mmio_hi_regions() override { return bus_drv_.mmio_hi_regions(); }
    RegionAllocator& pio_regions()     override { return bus_drv_.pio_regions(); }

    void AddRef() { RefCounted<PcieRoot>::AddRef(); }
    bool Release() __WARN_UNUSED_RESULT { return RefCounted<PcieRoot>::Release(); }

    // TODO(johngro) : Move Legacy IRQ swizling support out of PciePlatform and into roots
    // TODO(johngro) : Add support for RCRB (root complex register block)  Consider splitting
    // PcieRoot into PciRoot and PcieRoot (since PciRoots don't have RCRBs)

private:
    PcieRoot(PcieBusDriver& bus_drv, uint mbus_id);
    PcieBusDriver& bus_drv_;
};
