// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <dev/pcie_ref_counted.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <sys/types.h>

struct pcie_config_t;
class  RegionAllocator;
class  PciConfig;

// PcieUpstreamNode
//
// A class responsible for maintaining the state of a node in the graph of
// PCI/PCIe devices which can have downstream children.  PcieUpstreamNodes are
// not instantiated directly, instead they serve as the base class of
// PcieBridges and PcieRoots.
class PcieUpstreamNode {
public:
    enum class Type { ROOT, BRIDGE };
    virtual ~PcieUpstreamNode();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieUpstreamNode);

    // Require that derived classes implement ref counting.
    PCIE_REQUIRE_REFCOUNTED;

    fbl::RefPtr<PcieDevice> GetDownstream(uint ndx) { return bus_drv_.GetDownstream(*this, ndx); }
    PcieBusDriver& driver() { return bus_drv_; }

    Type type()           const { return type_; }
    uint managed_bus_id() const { return managed_bus_id_; }

    virtual RegionAllocator& mmio_lo_regions() = 0;
    virtual RegionAllocator& mmio_hi_regions() = 0;
    virtual RegionAllocator& pio_regions() = 0;

protected:
    friend class PcieBusDriver;
    PcieUpstreamNode(PcieBusDriver& bus_drv, Type type, uint mbus_id)
        : bus_drv_(bus_drv),
          type_(type),
          managed_bus_id_(mbus_id) { }

    void AllocateDownstreamBars();
    void DisableDownstream();
    void ScanDownstream();
    void UnplugDownstream();

    fbl::RefPtr<PcieDevice> ScanDevice(const PciConfig* cfg, uint dev_id, uint func_id);

private:

    PcieBusDriver& bus_drv_;         // TODO(johngro) : Eliminate this, see MG-325
    const Type     type_;
    const uint     managed_bus_id_;  // The ID of the downstream bus which this node manages.

    // An array of pointers for all the possible functions which exist on the
    // downstream bus of this node.
    //
    // TODO(johngro): Consider making this into a WAVLTree, indexed by the
    // concatenation of device and function ID instead of an array.
    fbl::RefPtr<PcieDevice> downstream_[PCIE_MAX_FUNCTIONS_PER_BUS];
};
