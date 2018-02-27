// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <object/pinned_memory_object.h>

#include <assert.h>
#include <err.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <zxcpp/new.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <object/bus_transaction_initiator_dispatcher.h>
#include <trace.h>

#define LOCAL_TRACE 0

zx_status_t PinnedMemoryObject::Create(const BusTransactionInitiatorDispatcher& bti,
                                       fbl::RefPtr<VmObject> vmo, size_t offset,
                                       size_t size, uint32_t perms,
                                       fbl::unique_ptr<PinnedMemoryObject>* out) {
    LTRACE_ENTRY;
    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset) && IS_PAGE_ALIGNED(size));

    // Commit the VMO range, in case it's not already committed.
    zx_status_t status = vmo->CommitRange(offset, size, nullptr);
    if (status != ZX_OK) {
        LTRACEF("vmo->CommitRange failed: %d\n", status);
        return status;
    }

    // Pin the memory to make sure it doesn't change from underneath us for the
    // lifetime of the created PMO.
    status = vmo->Pin(offset, size);
    if (status != ZX_OK) {
        LTRACEF("vmo->Pin failed: %d\n", status);
        return status;
    }

    uint64_t expected_addr = 0;
    auto check_contiguous = [](void* context, size_t offset, size_t index, paddr_t pa) {
        auto expected_addr = static_cast<uint64_t*>(context);
        if (index != 0 && pa != *expected_addr) {
            return ZX_ERR_NOT_FOUND;
        }
        *expected_addr = pa + PAGE_SIZE;
        return ZX_OK;
    };
    status = vmo->Lookup(offset, size, 0, check_contiguous, &expected_addr);
    bool is_contiguous = (status == ZX_OK);

    // Set up a cleanup function to undo the pin if we need to fail this
    // operation.
    auto unpin_vmo = fbl::MakeAutoCall([vmo, offset, size]() {
        vmo->Unpin(offset, size);
    });

    const size_t min_contig = bti.minimum_contiguity();
    DEBUG_ASSERT(fbl::is_pow2(min_contig));

    fbl::AllocChecker ac;
    const size_t num_addrs = ROUNDUP(size, min_contig) / min_contig;
    fbl::Array<dev_vaddr_t> addr_array(new (&ac) dev_vaddr_t[num_addrs], num_addrs);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::unique_ptr<PinnedMemoryObject> pmo(
            new (&ac) PinnedMemoryObject(bti, fbl::move(vmo), offset, size, is_contiguous,
                                         fbl::move(addr_array)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Now that the pmo object has been created, it is responsible for
    // unpinning.
    unpin_vmo.cancel();

    status = pmo->MapIntoIommu(perms);
    if (status != ZX_OK) {
        LTRACEF("MapIntoIommu failed: %d\n", status);
        return status;
    }

    *out = fbl::move(pmo);
    return ZX_OK;
}

// Used during initialization to set up the IOMMU state for this PMO.
zx_status_t PinnedMemoryObject::MapIntoIommu(uint32_t perms) {
    if (is_contiguous_) {
        constexpr dev_vaddr_t kInvalidAddr = 1;

        dev_vaddr_t vaddr_base = kInvalidAddr;
        // Usermode drivers assume that if they requested a contiguous buffer in
        // memory, then the physical addresses will be contiguous.  Return an
        // error if we can't acutally map the address contiguously.
        size_t remaining = size_;
        size_t curr_offset = offset_;
        while (remaining > 0) {
            dev_vaddr_t vaddr;
            size_t mapped_len;
            zx_status_t status = bti_.iommu()->Map(bti_.bti_id(), vmo_, curr_offset, remaining, perms,
                                                   &vaddr, &mapped_len);
            if (status != ZX_OK) {
                if (vaddr_base != kInvalidAddr) {
                    bti_.iommu()->Unmap(bti_.bti_id(), vaddr_base, curr_offset - offset_);
                }
                return status;
            }
            if (vaddr_base == kInvalidAddr) {
                vaddr_base = vaddr;
            } else if (vaddr != vaddr_base + curr_offset - offset_) {
                bti_.iommu()->Unmap(bti_.bti_id(), vaddr_base, curr_offset - offset_);
                bti_.iommu()->Unmap(bti_.bti_id(), vaddr, mapped_len);
                return ZX_ERR_INTERNAL;
            }

            curr_offset += mapped_len;
            remaining -= mapped_len;
        }

        mapped_addrs_[0] = vaddr_base;

        const size_t min_contig = bti_.minimum_contiguity();
        for (size_t i = 1; i < mapped_addrs_.size(); ++i) {
            mapped_addrs_[i] = mapped_addrs_[i - 1] + min_contig;
        }
        return ZX_OK;
    }

    size_t remaining = size_;
    uint64_t curr_offset = offset_;
    const size_t min_contig = bti_.minimum_contiguity();
    size_t next_addr_idx = 0;
    while (remaining > 0) {
        dev_vaddr_t vaddr;
        size_t mapped_len;
        zx_status_t status = bti_.iommu()->Map(bti_.bti_id(), vmo_, curr_offset, remaining, perms,
                                               &vaddr, &mapped_len);
        if (status != ZX_OK) {
            zx_status_t err = UnmapFromIommu();
            ASSERT(err == ZX_OK);
            return status;
        }

        // Break the range up into chunks of length |min_contig|
        size_t mapped_remaining = mapped_len;
        while (mapped_remaining > 0) {
            size_t addr_pages = fbl::min<size_t>(mapped_remaining, min_contig);
            mapped_addrs_[next_addr_idx] = vaddr;
            next_addr_idx++;
            vaddr += addr_pages;
            mapped_remaining -= addr_pages;
        }

        curr_offset += mapped_len;
        remaining -= fbl::min(mapped_len, remaining);
    }
    DEBUG_ASSERT(next_addr_idx == mapped_addrs_.size());

    return ZX_OK;
}

zx_status_t PinnedMemoryObject::UnmapFromIommu() {
    auto iommu = bti_.iommu();
    const uint64_t bus_txn_id = bti_.bti_id();

    if (mapped_addrs_[0] == UINT64_MAX) {
        // No work to do, nothing is mapped.
        return ZX_OK;
    }

    zx_status_t status = ZX_OK;
    if (is_contiguous_) {
        status = iommu->Unmap(bus_txn_id, mapped_addrs_[0], ROUNDUP(size_, PAGE_SIZE));
    } else {
        const size_t min_contig = bti_.minimum_contiguity();
        size_t remaining = ROUNDUP(size_, PAGE_SIZE);
        for (size_t i = 0; i < mapped_addrs_.size(); ++i) {
            dev_vaddr_t addr = mapped_addrs_[i];
            if (addr == UINT64_MAX) {
                break;
            }

            size_t size = fbl::min(remaining, min_contig);
            DEBUG_ASSERT(size == min_contig || i == mapped_addrs_.size() - 1);
            // Try to unmap all pages even if we get an error, and return the
            // first error encountered.
            zx_status_t err = iommu->Unmap(bus_txn_id, addr, size);
            DEBUG_ASSERT(err == ZX_OK);
            if (err != ZX_OK && status == ZX_OK) {
                status = err;
            }
            remaining -= size;
        }
    }

    // Clear this so we won't try again if this gets called again in the
    // destructor.
    InvalidateMappedAddrs();
    return status;
}

void PinnedMemoryObject::InvalidateMappedAddrs() {
    // Fill with a known invalid address to simplify cleanup of errors during
    // mapping
    for (size_t i = 0; i < mapped_addrs_.size(); ++i) {
        mapped_addrs_[i] = UINT64_MAX;
    }
}

PinnedMemoryObject::~PinnedMemoryObject() {
    zx_status_t status = UnmapFromIommu();
    ASSERT(status == ZX_OK);
    vmo_->Unpin(offset_, size_);
}

PinnedMemoryObject::PinnedMemoryObject(const BusTransactionInitiatorDispatcher& bti,
                                       fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                                       bool is_contiguous,
                                       fbl::Array<dev_vaddr_t> mapped_addrs)
    : vmo_(fbl::move(vmo)), offset_(offset), size_(size), is_contiguous_(is_contiguous), bti_(bti),
      mapped_addrs_(fbl::move(mapped_addrs)) {

    InvalidateMappedAddrs();
}
