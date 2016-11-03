// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

struct pcie_config_t;
class  RegionAllocator;

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

    mxtl::RefPtr<PcieDevice> GetDownstream(uint ndx) { return bus_drv_.GetDownstream(*this, ndx); }
    PcieBusDriver& driver() { return bus_drv_; }

    Type type()           const { return type_; }
    uint managed_bus_id() const { return managed_bus_id_; }

    virtual RegionAllocator& mmio_lo_regions() = 0;
    virtual RegionAllocator& mmio_hi_regions() = 0;
    virtual RegionAllocator& pio_regions() = 0;

    // Explicit implementation of AddRef and Release.
    //
    // We want to be able to hold RefPtrs to PcieUpstreamNodes, so normally we
    // would just derive from mxtl::RefCounted<>.  Unfortunately, PcieBridges
    // are both PcieUpstreamNodes as well as PcieDevices, and PcieDevices are
    // already RefCounted.  One could solve this problem with virtual
    // inheritance, but one runs the risk of being murdered by one's colleagues
    // if one were to try such a thing.
    //
    // Instead of getting murdered, we ensure that all of the derived classes of
    // UpstreamNode (Bridge/Device and Root) inherit from RefCounted, and then
    // have explicit implementations of AddRef/Release which use type to perform
    // a safe downcast to our derived class and then call their AddRef/Release
    // implementation.  There is a minor performance penalty for this, but since
    // external users deal almost exclusively with PcieDevices and nothing else,
    // it is not really on the critical path.  Plus, it is much better than
    // getting murdered.
    void AddRef();
    bool Release() __WARN_UNUSED_RESULT;

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

    mxtl::RefPtr<PcieDevice> ScanDevice(pcie_config_t* cfg, uint dev_id, uint func_id);

private:

    PcieBusDriver& bus_drv_;         // TODO(johngro) : Eliminate this, see MG-325
    const Type     type_;
    const uint     managed_bus_id_;  // The ID of the downstream bus which this node manages.

    // An array of pointers for all the possible functions which exist on the
    // downstream bus of this node.
    //
    // TODO(johngro): Consider making this into a WAVLTree, indexed by the
    // concatenation of device and function ID instead of an array.
    mxtl::RefPtr<PcieDevice> downstream_[PCIE_MAX_FUNCTIONS_PER_BUS];
};
