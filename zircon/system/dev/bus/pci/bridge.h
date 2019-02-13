// Copyright 2019 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include "common.h"
#include "config.h"
#include "device.h"
#include "ref_counted.h"
#include "upstream_node.h"
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <region-alloc/region-alloc.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

namespace pci {

class PciRegionAllocation final : public PciAllocation {
public:
    PciRegionAllocation(RegionAllocator::Region::UPtr&& region) : region_(std::move(region)){};
    zx_paddr_t base() final { return region_->base; }
    size_t size() final { return region_->size; }

private:
    const RegionAllocator::Region::UPtr region_;
};

// PciRegionAllocators are a wrapper around RegionAllocators to allow Bridge
// objects to implement the UpstreamNode interface by using regions that they
// are provided by nodes further upstream. They hand out PciRegionAllocations
// which will release allocations back upstream if they go out of scope.
class PciRegionAllocator : public PciAllocator {
public:
    PciRegionAllocator() = default;
    // These should not be copied, assigned, or moved
    PciRegionAllocator(const PciRegionAllocator&) = delete;
    PciRegionAllocator(PciRegionAllocator&&) = delete;
    PciRegionAllocator& operator=(const PciRegionAllocator&) = delete;
    PciRegionAllocator& operator=(PciRegionAllocator&&) = delete;

    zx_status_t GetRegion(zx_paddr_t base,
                          size_t size,
                          fbl::unique_ptr<PciAllocation>* alloc) final {
        RegionAllocator::Region::UPtr region_uptr;
        zx_status_t status = allocator_.GetRegion({.base = base, .size = size}, region_uptr);
        if (status != ZX_OK) {
            return status;
        }

        pci_tracef("bridge: assigned [ %#lx-%#lx ] to downstream bridge\n", base, base+size);
        fbl::AllocChecker ac;
        *alloc = fbl::unique_ptr(new (&ac) PciRegionAllocation(std::move(region_uptr)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        return ZX_OK;
    }

    zx_status_t AddAddressSpace(fbl::unique_ptr<PciAllocation> alloc) final {
        backing_alloc_ = std::move(alloc);
        auto base = backing_alloc_->base();
        auto size = backing_alloc_->size();
        return allocator_.AddRegion({.base = base, .size = size});
    }

    void SetRegionPool(RegionAllocator::RegionPool::RefPtr pool) { allocator_.SetRegionPool(pool); }

private:
    // This PciAllocation is the object handed to the bridge by the upstream node
    // and holds a reservation for that address space in the upstream bridge's window
    // for use downstream this bridge.
    fbl::unique_ptr<PciAllocation> backing_alloc_;
    RegionAllocator allocator_;
};

class Bridge : public pci::Device, public UpstreamNode {
public:
    static zx_status_t Create(fbl::RefPtr<Config>&& config,
                              UpstreamNode* upstream,
                              BusLinkInterface* bli,
                              uint8_t mbus_id,
                              fbl::RefPtr<pci::Bridge>* out_bridge);
    // Derived device objects need to have refcounting implemented
    PCI_IMPLEMENT_REFCOUNTED;

    // Disallow copying, assigning and moving.
    Bridge(const Bridge&) = delete;
    Bridge(Bridge&&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge& operator=(Bridge&&) = delete;

    // UpstreamNode overrides
    PciAllocator& mmio_regions() final { return mmio_regions_; }
    PciAllocator& pf_mmio_regions() final { return pf_mmio_regions_; }
    PciAllocator& pio_regions() final { return pio_regions_; }

    // Property accessors
    uint64_t pf_mem_base() const { return pf_mem_base_; }
    uint64_t pf_mem_limit() const { return pf_mem_limit_; }
    uint32_t mem_base() const { return mem_base_; }
    uint32_t mem_limit() const { return mem_limit_; }
    uint32_t io_base() const { return io_base_; }
    uint32_t io_limit() const { return io_limit_; }
    bool supports_32bit_pio() const { return supports_32bit_pio_; }

    // Device overrides
    void Dump() const final;
    void Unplug() final TA_EXCL(dev_lock_);

protected:
    zx_status_t AllocateBars() final TA_EXCL(dev_lock_);
    zx_status_t AllocateBridgeWindowsLocked() TA_REQ(dev_lock_);
    void Disable() final;

private:
    Bridge(fbl::RefPtr<Config>&&, UpstreamNode* upstream, BusLinkInterface* bli, uint8_t mbus_id);
    zx_status_t Init() TA_EXCL(dev_lock_);

    zx_status_t ParseBusWindowsLocked() TA_REQ(dev_lock_);

    PciRegionAllocator mmio_regions_;
    PciRegionAllocator pf_mmio_regions_;
    PciRegionAllocator pio_regions_;

    uint64_t pf_mem_base_;
    uint64_t pf_mem_limit_;
    uint32_t mem_base_;
    uint32_t mem_limit_;
    uint32_t io_base_;
    uint32_t io_limit_;
    bool supports_32bit_pio_;
};

}; // namespace pci
