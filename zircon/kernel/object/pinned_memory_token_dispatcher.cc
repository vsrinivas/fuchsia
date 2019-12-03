// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/pinned_memory_token_dispatcher.h"

#include <assert.h>
#include <err.h>
#include <lib/counters.h>
#include <trace.h>

#include <new>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <object/bus_transaction_initiator_dispatcher.h>
#include <vm/pinned_vm_object.h>
#include <vm/vm.h>
#include <vm/vm_object.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_pinned_memory_token_create_count, "dispatcher.pinned_memory_token.create")
KCOUNTER(dispatcher_pinned_memory_token_destroy_count, "dispatcher.pinned_memory_token.destroy")

zx_status_t PinnedMemoryTokenDispatcher::Create(fbl::RefPtr<BusTransactionInitiatorDispatcher> bti,
                                                PinnedVmObject pinned_vmo, uint32_t perms,
                                                KernelHandle<PinnedMemoryTokenDispatcher>* handle,
                                                zx_rights_t* rights) {
  LTRACE_ENTRY;
  DEBUG_ASSERT(IS_PAGE_ALIGNED(pinned_vmo.offset()) && IS_PAGE_ALIGNED(pinned_vmo.size()));

  size_t num_addrs;
  if (!pinned_vmo.vmo()->is_contiguous()) {
    const size_t min_contig = bti->minimum_contiguity();
    DEBUG_ASSERT(fbl::is_pow2(min_contig));

    num_addrs = ROUNDUP(pinned_vmo.size(), min_contig) / min_contig;
  } else {
    num_addrs = 1;
  }

  fbl::AllocChecker ac;
  fbl::Array<dev_vaddr_t> addr_array(new (&ac) dev_vaddr_t[num_addrs], num_addrs);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  KernelHandle new_handle(fbl::AdoptRef(new (&ac) PinnedMemoryTokenDispatcher(
      ktl::move(bti), ktl::move(pinned_vmo), ktl::move(addr_array))));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = new_handle.dispatcher()->MapIntoIommu(perms);
  if (status != ZX_OK) {
    LTRACEF("MapIntoIommu failed: %d\n", status);
    return status;
  }

  // Create must be called with the BTI's lock held, so this is safe to
  // invoke.
  const fbl::RefPtr<PinnedMemoryTokenDispatcher>& dispatcher = new_handle.dispatcher();
  AssertHeld(*dispatcher->bti_->get_lock());
  dispatcher->bti_->AddPmoLocked(new_handle.dispatcher().get());
  dispatcher->initialized_ = true;

  *handle = ktl::move(new_handle);
  *rights = default_rights();
  return ZX_OK;
}

// Used during initialization to set up the IOMMU state for this PMT.
//
// We disable thread-safety analysis here, because this is part of the
// initialization routine before other threads have access to this dispatcher.
zx_status_t PinnedMemoryTokenDispatcher::MapIntoIommu(uint32_t perms) TA_NO_THREAD_SAFETY_ANALYSIS {
  DEBUG_ASSERT(!initialized_);

  const uint64_t bti_id = bti_->bti_id();
  const size_t min_contig = bti_->minimum_contiguity();
  if (pinned_vmo_.vmo()->is_contiguous()) {
    dev_vaddr_t vaddr;
    size_t mapped_len;

    // Usermode drivers assume that if they requested a contiguous buffer in
    // memory, then the physical addresses will be contiguous.  Return an
    // error if we can't actually map the address contiguously.
    zx_status_t status =
        bti_->iommu()->MapContiguous(bti_id, pinned_vmo_.vmo(), pinned_vmo_.offset(),
                                     pinned_vmo_.size(), perms, &vaddr, &mapped_len);
    if (status != ZX_OK) {
      return status;
    }

    DEBUG_ASSERT(vaddr % min_contig == 0);
    DEBUG_ASSERT(mapped_addrs_.size() == 1);
    mapped_addrs_[0] = vaddr;
    return ZX_OK;
  }

  size_t remaining = pinned_vmo_.size();
  uint64_t curr_offset = pinned_vmo_.offset();
  size_t next_addr_idx = 0;
  while (remaining > 0) {
    dev_vaddr_t vaddr;
    size_t mapped_len;
    zx_status_t status = bti_->iommu()->Map(bti_id, pinned_vmo_.vmo(), curr_offset, remaining,
                                            perms, &vaddr, &mapped_len);
    if (status != ZX_OK) {
      // We'll revert any partial mappings in on_zero_handles().
      return status;
    }

    // Ensure we don't end up with any non-terminal chunks that are not |min_contig| in
    // length.
    DEBUG_ASSERT(mapped_len % min_contig == 0 || remaining == mapped_len);

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

zx_status_t PinnedMemoryTokenDispatcher::UnmapFromIommuLocked() {
  auto iommu = bti_->iommu();
  const uint64_t bus_txn_id = bti_->bti_id();

  if (mapped_addrs_[0] == UINT64_MAX) {
    // No work to do, nothing is mapped.
    return ZX_OK;
  }

  zx_status_t status = ZX_OK;
  if (pinned_vmo_.vmo()->is_contiguous()) {
    status = iommu->Unmap(bus_txn_id, mapped_addrs_[0], pinned_vmo_.size());
  } else {
    const size_t min_contig = bti_->minimum_contiguity();
    size_t remaining = pinned_vmo_.size();
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

  return status;
}

void PinnedMemoryTokenDispatcher::Unpin() {
  Guard<fbl::Mutex> guard{get_lock()};
  explicitly_unpinned_ = true;

  // Unmap the memory prior to unpinning to prevent continued access.
  zx_status_t status = UnmapFromIommuLocked();
  ASSERT(status == ZX_OK);

  // Move the pinned vmo to a temporary and to let its dtor unpin.
  auto destroy = ktl::move(pinned_vmo_);
}

void PinnedMemoryTokenDispatcher::InvalidateMappedAddrsLocked() {
  // Fill with a known invalid address to simplify cleanup of errors during
  // mapping
  for (size_t i = 0; i < mapped_addrs_.size(); ++i) {
    mapped_addrs_[i] = UINT64_MAX;
  }
}

void PinnedMemoryTokenDispatcher::on_zero_handles() {
  Guard<fbl::Mutex> guard{get_lock()};

  if (!explicitly_unpinned_ && initialized_) {
    // The user failed to call zx_pmt_unpin. Unmap the memory to prevent continued access, but leave
    // the VMO pinned and use the quarantine mechanism to protect against stray DMA.
    zx_status_t status = UnmapFromIommuLocked();
    ASSERT(status == ZX_OK);

    bti_->Quarantine(fbl::RefPtr(this));
  }
}

PinnedMemoryTokenDispatcher::~PinnedMemoryTokenDispatcher() {
  kcounter_add(dispatcher_pinned_memory_token_destroy_count, 1);

  if (initialized_) {
    bti_->RemovePmo(this);
  }
}

PinnedMemoryTokenDispatcher::PinnedMemoryTokenDispatcher(
    fbl::RefPtr<BusTransactionInitiatorDispatcher> bti, PinnedVmObject pinned_vmo,
    fbl::Array<dev_vaddr_t> mapped_addrs)
    : pinned_vmo_(ktl::move(pinned_vmo)),
      bti_(ktl::move(bti)),
      mapped_addrs_(ktl::move(mapped_addrs)) {
  DEBUG_ASSERT(pinned_vmo_.vmo() != nullptr);
  InvalidateMappedAddrsLocked();
  kcounter_add(dispatcher_pinned_memory_token_create_count, 1);
}

zx_status_t PinnedMemoryTokenDispatcher::EncodeAddrs(bool compress_results, bool contiguous,
                                                     dev_vaddr_t* mapped_addrs,
                                                     size_t mapped_addrs_count) {
  Guard<fbl::Mutex> guard{get_lock()};

  const fbl::Array<dev_vaddr_t>& pmo_addrs = mapped_addrs_;
  const size_t found_addrs = pmo_addrs.size();
  if (compress_results) {
    if (pinned_vmo_.vmo()->is_contiguous()) {
      const size_t min_contig = bti_->minimum_contiguity();
      DEBUG_ASSERT(fbl::is_pow2(min_contig));
      DEBUG_ASSERT(found_addrs == 1);

      uint64_t num_addrs = ROUNDUP(pinned_vmo_.size(), min_contig) / min_contig;
      if (num_addrs != mapped_addrs_count) {
        return ZX_ERR_INVALID_ARGS;
      }
      for (uint64_t i = 0; i < num_addrs; i++) {
        mapped_addrs[i] = pmo_addrs[0] + min_contig * i;
      }
    } else {
      if (found_addrs != mapped_addrs_count) {
        return ZX_ERR_INVALID_ARGS;
      }
      memcpy(mapped_addrs, pmo_addrs.data(), found_addrs * sizeof(dev_vaddr_t));
    }
  } else if (contiguous) {
    if (mapped_addrs_count != 1 || !pinned_vmo_.vmo()->is_contiguous()) {
      return ZX_ERR_INVALID_ARGS;
    }
    *mapped_addrs = pmo_addrs[0];
  } else {
    const size_t num_pages = pinned_vmo_.size() / PAGE_SIZE;
    if (num_pages != mapped_addrs_count) {
      return ZX_ERR_INVALID_ARGS;
    }
    const size_t min_contig =
        pinned_vmo_.vmo()->is_contiguous() ? pinned_vmo_.size() : bti_->minimum_contiguity();
    size_t next_idx = 0;
    for (size_t i = 0; i < found_addrs; ++i) {
      dev_vaddr_t extent_base = pmo_addrs[i];
      for (dev_vaddr_t addr = extent_base; addr < extent_base + min_contig && next_idx < num_pages;
           addr += PAGE_SIZE, ++next_idx) {
        mapped_addrs[next_idx] = addr;
      }
    }
  }
  return ZX_OK;
}
