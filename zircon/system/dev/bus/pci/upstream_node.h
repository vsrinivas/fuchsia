// Copyright 2019 The Fuchsia Authors
// Copyright (c) 2019, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

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

// A forward declaration until pci::Device is ported to userspace.
class Device : public fbl::DoublyLinkedListable<pci::Device*> {};

class PciAllocator;
class UpstreamNode {
public:
    enum class Type { ROOT,
                      BRIDGE };
    virtual ~UpstreamNode();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(UpstreamNode);

    Type type() const { return type_; }
    uint32_t managed_bus_id() const { return managed_bus_id_; }

    virtual PciAllocator& pf_mmio_regions() = 0;
    virtual PciAllocator& mmio_lo_regions() = 0;
    virtual PciAllocator& mmio_hi_regions() = 0;
    virtual PciAllocator& pio_regions() = 0;

    zx_status_t LinkDevice(pci::Device* device);
    zx_status_t UnlinkDevice(pci::Device* device);

protected:
    UpstreamNode(Type type, uint32_t mbus_id)
        : type_(type),
          managed_bus_id_(mbus_id) {}

    virtual void AllocateDownstreamBars();
    virtual void DisableDownstream();
    virtual void ScanDownstream();
    virtual void UnplugDownstream();

    // The list of all devices immediately under this root/bridge.
    fbl::DoublyLinkedList<pci::Device*> downstream_list_;

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

    virtual ~PciAllocation() {}
    virtual zx_paddr_t base() = 0;
    virtual size_t size() = 0;

protected:
    PciAllocation() = default;
};

class PciRootAllocation final : public PciAllocation {
public:
    PciRootAllocation(zx_paddr_t base, size_t size)
        : base_(base), size_(size) {}
    virtual ~PciRootAllocation() {}
    zx_paddr_t base() final { return base_; }
    size_t size() final { return size_; }

private:
    const zx_paddr_t base_;
    const size_t size_;
};

class PciRegionAllocation final : public PciAllocation {
public:
    PciRegionAllocation(RegionAllocator::Region::UPtr&& region)
        : region_(std::move(region)){};
    zx_paddr_t base() final { return region_->base; }
    size_t size() final { return region_->size; }

private:
    RegionAllocator::Region::UPtr region_;
};

class PciAllocator {
public:
    virtual ~PciAllocator() {}
    virtual zx_status_t GetRegion(zx_paddr_t base,
                                  size_t size,
                                  fbl::unique_ptr<PciAllocation> out_alloc) = 0;
    virtual zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) = 0;

protected:
    PciAllocator() {};
};

// PciRootAllocators are an implementation of PciAllocator designed
// to use the Pciroot protocol for allocation, fulfilling the requirements
// for a PciRoot to implement the UpstreamNode interface.
class PciRootAllocator : public PciAllocator {
public:
    PciRootAllocator(ddk::PcirootProtocolClient* proto, pci_address_space_t type, bool low)
        : pciroot_(proto), type_(type), low_(low) {}
    // These should not be copied, assigned, or moved
    PciRootAllocator(const PciRootAllocator&) = delete;
    PciRootAllocator(PciRootAllocator&&) = delete;
    PciRootAllocator& operator=(const PciRootAllocator&) = delete;
    PciRootAllocator& operator=(PciRootAllocator&&) = delete;

    virtual ~PciRootAllocator(){};
    zx_status_t GetRegion(zx_paddr_t base,
                          size_t size,
                          fbl::unique_ptr<PciAllocation> alloc) final;
    zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) final;

private:
    // The bus driver outlives allocator objects.
    ddk::PcirootProtocolClient* const pciroot_;
    const pci_address_space_t type_;
    // This denotes whether this allocator requests memory < 4GB. More detail
    // can be found in the explanation for mmio_lo in root.h.
    const bool low_;
};

// PciRegionAllocators are a wrapper around RegionAllocators to allow
// Bridge objects to implement the UpstreamNode interface by using regions
// they get other bridges & the root upstream..
//
// TODO(cja) implement this when bridge is ported over in the near future.
class PciRegionAllocator : public PciAllocator {
public:
    PciRegionAllocator(){};
    // These should not be copied, assigned, or moved
    PciRegionAllocator(const PciRegionAllocator&) = delete;
    PciRegionAllocator(PciRegionAllocator&&) = delete;
    PciRegionAllocator& operator=(const PciRegionAllocator&) = delete;
    PciRegionAllocator& operator=(PciRegionAllocator&&) = delete;

    zx_status_t GetRegion(zx_paddr_t base,
                          size_t size,
                          fbl::unique_ptr<PciAllocation> alloc) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) final {
        return ZX_ERR_NOT_SUPPORTED;
    }
};

} // namespace pci
