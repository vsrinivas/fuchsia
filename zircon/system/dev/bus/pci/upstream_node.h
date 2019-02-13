// Copyright 2019 The Fuchsia Authors
// Copyright (c) 2019, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include "device.h"
#include "ref_counted.h"
#include <ddktl/protocol/pciroot.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <region-alloc/region-alloc.h>
#include <sys/types.h>
#include <zircon/types.h>

// UpstreamNode
//
// A class responsible for maintaining the state of a node in the graph of
// PCI/PCIe devices which can have downstream children.  UpstreamNodes are
// not instantiated directly, instead they serve as the base class of
// PCI/PCIe bridges and roots.

namespace pci {

class PciAllocator;
class UpstreamNode {
public:
    enum class Type { ROOT, BRIDGE };
    // UpstreamNode must have refcounting implemented by its derived classes Root or Bridge
    PCI_REQUIRE_REFCOUNTED;

    // Disallow copying, assigning and moving.
    UpstreamNode(const UpstreamNode&) = delete;
    UpstreamNode(UpstreamNode&&) = delete;
    UpstreamNode& operator=(const UpstreamNode&) = delete;
    UpstreamNode& operator=(UpstreamNode&&) = delete;

    Type type() const { return type_; }
    uint32_t managed_bus_id() const { return managed_bus_id_; }

    virtual PciAllocator& pf_mmio_regions() = 0;
    virtual PciAllocator& mmio_regions() = 0;
    virtual PciAllocator& pio_regions() = 0;

    void LinkDevice(pci::Device* device) { downstream_.push_back(device); }
    void UnlinkDevice(pci::Device* device) { downstream_.erase(*device); }

protected:
    UpstreamNode(Type type, uint32_t mbus_id) : type_(type), managed_bus_id_(mbus_id) {}
    virtual ~UpstreamNode() = default;

    virtual void AllocateDownstreamBars();
    // Disable all devices directly connected to this bridge.
    virtual void DisableDownstream();
    // Unplug all devices directly connected to this bridge.
    virtual void UnplugDownstream();
    // The list of all devices immediately under this root/bridge.
    fbl::DoublyLinkedList<pci::Device*> downstream_;

private:
    const Type type_;
    const uint32_t managed_bus_id_; // The ID of the downstream bus which this node manages.
};

// PciAllocations and PciAllocators are concepts internal to UpstreamNodes which
// track address space allocations across roots and bridges. PciAllocator is an
// interface for roots and bridges to provide allocators to downstream bridges
// for their own allocations. Roots allocate across the PciRoot protocol so the
// dtors of PciRootAllocations will make a protocol call to release the address
// space if for some reason a root's allocations go out of scope. A bridge works
// similarly, except its allocations from from a bridge or upstream root's
// region allocators, and holds a given region for its lifecycle. When it is
// released the region can go through the normal region lifecycle and be
// released back to the region allocator.
class PciAllocation {
public:
    // These should not be copied, assigned, or moved
    PciAllocation(const PciAllocation&) = delete;
    PciAllocation(PciAllocation&) = delete;
    PciAllocation& operator=(const PciAllocation&) = delete;
    PciAllocation& operator=(PciAllocation&&) = delete;

    virtual ~PciAllocation() = default;
    virtual zx_paddr_t base() = 0;
    virtual size_t size() = 0;

protected:
    PciAllocation() = default;
};

class PciAllocator {
public:
    virtual ~PciAllocator() = default;
    virtual zx_status_t GetRegion(zx_paddr_t base,
                                  size_t size,
                                  fbl::unique_ptr<PciAllocation>* out_alloc) = 0;
    virtual zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) = 0;

protected:
    PciAllocator() = default;
};

} // namespace pci
