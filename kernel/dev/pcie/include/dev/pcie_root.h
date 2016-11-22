// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie_bus_driver.h>
#include <dev/pcie_ref_counted.h>
#include <dev/pcie_upstream_node.h>
#include <magenta/compiler.h>
#include <mxtl/macros.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/ref_ptr.h>

class PcieRoot : public mxtl::WAVLTreeContainable<mxtl::RefPtr<PcieRoot>>,
                 public PcieUpstreamNode
{
public:
    static mxtl::RefPtr<PcieRoot> Create(PcieBusDriver& bus_drv, uint managed_bus_id);

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieRoot);

    // Implement ref counting, do not let derived classes override.
    PCIE_IMPLEMENT_REFCOUNTED;

    PcieBusDriver& driver() { return bus_drv_; }
    RegionAllocator& mmio_lo_regions() override { return bus_drv_.mmio_lo_regions(); }
    RegionAllocator& mmio_hi_regions() override { return bus_drv_.mmio_hi_regions(); }
    RegionAllocator& pio_regions()     override { return bus_drv_.pio_regions(); }


    // WAVL-tree Index
    uint GetKey() const { return managed_bus_id(); }
    // TODO(johngro) : Move Legacy IRQ swizling support out of PciePlatform and into roots
    // TODO(johngro) : Add support for RCRB (root complex register block)  Consider splitting
    // PcieRoot into PciRoot and PcieRoot (since PciRoots don't have RCRBs)

private:
    PcieRoot(PcieBusDriver& bus_drv, uint mbus_id);
    PcieBusDriver& bus_drv_;
};
