// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_object_paged.h"

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/move.h>
#include <vm/bootreserve.h>
#include <vm/fault.h>
#include <vm/page_source.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_cow_pages.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

namespace {

KCOUNTER(vmo_attribution_queries, "vm.attributed_pages.object.queries")
KCOUNTER(vmo_attribution_cache_hits, "vm.attributed_pages.object.cache_hits")
KCOUNTER(vmo_attribution_cache_misses, "vm.attributed_pages.object.cache_misses")

}  // namespace

VmObjectPaged::VmObjectPaged(uint32_t options, fbl::RefPtr<VmHierarchyState> hierarchy_state)
    : VmObject(ktl::move(hierarchy_state)), options_(options) {
  LTRACEF("%p\n", this);
}

VmObjectPaged::~VmObjectPaged() {
  canary_.Assert();

  LTRACEF("%p\n", this);

  if (!cow_pages_) {
    // Initialization didn't finish. This is not in the global list and any complex destruction can
    // all be skipped.
    DEBUG_ASSERT(!InGlobalList());
    return;
  }

  RemoveFromGlobalList();

  Guard<Mutex> guard{&lock_};

  if (is_contiguous() && !is_slice()) {
    // A contiguous VMO either has all its pages committed and pinned or, if creation failed, no
    // pages committed and pinned. Check if we are in the failure case by looking up the first page.
    if (GetPageLocked(0, 0, nullptr, nullptr, nullptr, nullptr) == ZX_OK) {
      cow_pages_locked()->UnpinLocked(0, size_locked());
    }
  }

  AssertHeld(hierarchy_state_ptr_->lock_ref());
  hierarchy_state_ptr_->IncrementHierarchyGenerationCountLocked();

  cow_pages_locked()->set_paged_backlink_locked(nullptr);

  // Re-home all our children with any parent that we have.
  while (!children_list_.is_empty()) {
    VmObject* c = &children_list_.front();
    children_list_.pop_front();
    VmObjectPaged* child = reinterpret_cast<VmObjectPaged*>(c);
    child->parent_ = parent_;
    if (parent_) {
      // Ignore the return since 'this' is a child so we know we are not transitioning from 0->1
      // children.
      bool __UNUSED notify = parent_->AddChildLocked(child);
      DEBUG_ASSERT(!notify);
    }
  }

  if (parent_) {
    // As parent_ is a raw pointer we must ensure that if we call a method on it that it lives long
    // enough. To do so we attempt to upgrade it to a refptr, which could fail if it's already
    // slated for deletion.
    fbl::RefPtr<VmObjectPaged> parent = fbl::MakeRefPtrUpgradeFromRaw(parent_, guard);
    if (parent) {
      // Holding refptr, can safely pass in the guard to RemoveChild.
      parent->RemoveChild(this, guard.take());
    } else {
      // parent is up for deletion and so there's no need to use RemoveChild since there is no
      // user dispatcher to notify anyway and so just drop ourselves to keep the hierarchy correct.
      parent_->DropChildLocked(this);
    }
  }
}

void VmObjectPaged::HarvestAccessedBits() {
  canary_.Assert();

  Guard<Mutex> guard{lock()};
  // If there is no root page source, then we have nothing worth harvesting bits from.
  if (!cow_pages_locked()->is_pager_backed_locked()) {
    return;
  }

  fbl::Function<bool(vm_page_t*, uint64_t)> f = [this](vm_page_t* p, uint64_t offset) {
    AssertHeld(lock_);
    // Skip the zero page as we are never going to evict it and initial zero pages will not be
    // returned by GetPageLocked down below.
    if (p == vm_get_zero_page()) {
      return false;
    }
    // Use GetPageLocked to perform page lookup. Pass neither software fault, hardware fault or
    // write to prevent any committing or copy-on-write behavior. This will just cause the page to
    // be looked up, and its location in any pager_backed queues updated.
    __UNUSED vm_page_t* out;
    __UNUSED zx_status_t result =
        cow_pages_locked()->GetPageLocked(offset, 0, nullptr, nullptr, &out, nullptr);
    // We are in this callback because there is a physical page mapped into the hardware page table
    // attributed to this vmo. If we cannot find it, or it isn't the page we expect, then something
    // has gone horribly wrong.
    DEBUG_ASSERT(result == ZX_OK);
    DEBUG_ASSERT(out == p);
    return true;
  };
  for (auto& m : mapping_list_) {
    if (m.aspace()->is_user()) {
      AssertHeld(*m.object_lock());
      __UNUSED zx_status_t result = m.HarvestAccessVmoRangeLocked(0, size_locked(), f);
      // There's no way we should be harvesting an invalid range as that would imply that this
      // mapping is invalid.
      DEBUG_ASSERT(result == ZX_OK);
    }
  }
}

bool VmObjectPaged::CanDedupZeroPagesLocked() {
  canary_.Assert();

  // Skip uncached VMOs as we cannot efficiently scan them.
  if ((cache_policy_ & ZX_CACHE_POLICY_MASK) != ZX_CACHE_POLICY_CACHED) {
    return false;
  }

  // Skip any VMOs that have non user mappings as we cannot safely remove write permissions from
  // them and indicates this VMO is actually in use by the kernel and we probably would not want to
  // perform zero page de-duplication on it even if we could.
  for (auto& m : mapping_list_) {
    if (!m.aspace()->is_user()) {
      return false;
    }
  }

  // Okay to dedup from this VMO.
  return true;
}

uint32_t VmObjectPaged::ScanForZeroPages(bool reclaim) {
  canary_.Assert();

  Guard<Mutex> guard{lock()};

  // Skip uncached VMOs as we cannot efficiently scan them.
  if ((cache_policy_ & ZX_CACHE_POLICY_MASK) != ZX_CACHE_POLICY_CACHED) {
    return 0;
  }

  // Skip any VMOs that have non user mappings as we cannot safely remove write permissions from
  // them and indicates this VMO is actually in use by the kernel and we probably would not want to
  // perform zero page de-duplication on it even if we could.
  for (auto& m : mapping_list_) {
    if (!m.aspace()->is_user()) {
      return 0;
    }
    // Remove write from the mapping to ensure it's not being concurrently modified.
    AssertHeld(*m.object_lock());
    m.AspaceRemoveWriteVmoRangeLocked(0, size_locked());
  }

  uint32_t count = cow_pages_locked()->ScanForZeroPagesLocked(reclaim);

  if (reclaim && count > 0) {
    IncrementHierarchyGenerationCountLocked();
  }

  return count;
}

zx_status_t VmObjectPaged::CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                        fbl::RefPtr<VmObjectPaged>* obj) {
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto state = fbl::MakeRefCountedChecked<VmHierarchyState>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<VmCowPages> cow_pages;
  status = VmCowPages::Create(state, pmm_alloc_flags, size, &cow_pages);
  if (status != ZX_OK) {
    return status;
  }

  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, ktl::move(state)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // This creation has succeeded. Must wire up the cow pages and *then* place in the globals list.
  {
    Guard<Mutex> guard{&vmo->lock_};
    AssertHeld(cow_pages->lock_ref());
    cow_pages->set_paged_backlink_locked(vmo.get());
    vmo->cow_pages_ = ktl::move(cow_pages);
  }
  vmo->AddToGlobalList();

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::Create(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                  fbl::RefPtr<VmObjectPaged>* obj) {
  if (options & kContiguous) {
    // Force callers to use CreateContiguous() instead.
    return ZX_ERR_INVALID_ARGS;
  }

  return CreateCommon(pmm_alloc_flags, options, size, obj);
}

zx_status_t VmObjectPaged::CreateContiguous(uint32_t pmm_alloc_flags, uint64_t size,
                                            uint8_t alignment_log2,
                                            fbl::RefPtr<VmObjectPaged>* obj) {
  DEBUG_ASSERT(alignment_log2 < sizeof(uint64_t) * 8);
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<VmObjectPaged> vmo;
  status = CreateCommon(pmm_alloc_flags, kContiguous, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  if (size == 0) {
    *obj = ktl::move(vmo);
    return ZX_OK;
  }

  // allocate the pages
  list_node page_list;
  list_initialize(&page_list);

  size_t num_pages = size / PAGE_SIZE;
  paddr_t pa;
  status = pmm_alloc_contiguous(num_pages, pmm_alloc_flags, alignment_log2, &pa, &page_list);
  if (status != ZX_OK) {
    LTRACEF("failed to allocate enough pages (asked for %zu)\n", num_pages);
    return ZX_ERR_NO_MEMORY;
  }
  Guard<Mutex> guard{&vmo->lock_};
  // add them to the appropriate range of the object, this takes ownership of all the pages
  // regardless of outcome.
  status = vmo->cow_pages_locked()->AddNewPagesLocked(0, &page_list);
  if (status != ZX_OK) {
    return status;
  }

  // We already added the pages, so this will just cause them to be pinned.
  status = vmo->cow_pages_locked()->PinRangeLocked(0, size);
  if (status != ZX_OK) {
    // Decommit the range so the destructor doesn't attempt to unpin.
    vmo->DecommitRangeLocked(0, size);
    return status;
  }

  *obj = ktl::move(vmo);
  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateFromWiredPages(const void* data, size_t size, bool exclusive,
                                                fbl::RefPtr<VmObjectPaged>* obj) {
  LTRACEF("data %p, size %zu\n", data, size);

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = CreateCommon(PMM_ALLOC_FLAG_ANY, 0, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  if (size > 0) {
    ASSERT(IS_PAGE_ALIGNED(size));
    ASSERT(IS_PAGE_ALIGNED(reinterpret_cast<uintptr_t>(data)));

    // Do a direct lookup of the physical pages backing the range of
    // the kernel that these addresses belong to and jam them directly
    // into the VMO.
    //
    // NOTE: This relies on the kernel not otherwise owning the pages.
    // If the setup of the kernel's address space changes so that the
    // pages are attached to a kernel VMO, this will need to change.

    paddr_t start_paddr = vaddr_to_paddr(data);
    ASSERT(start_paddr != 0);

    Guard<Mutex> guard{&vmo->lock_};

    for (size_t count = 0; count < size / PAGE_SIZE; count++) {
      paddr_t pa = start_paddr + count * PAGE_SIZE;
      vm_page_t* page = paddr_to_vm_page(pa);
      ASSERT(page);

      if (page->state() == VM_PAGE_STATE_WIRED) {
        boot_reserve_unwire_page(page);
      } else {
        // This function is only valid for memory in the boot image,
        // which should all be wired.
        panic("page used to back static vmo in unusable state: paddr %#" PRIxPTR " state %u\n", pa,
              page->state());
      }
      status = vmo->cow_pages_locked()->AddNewPageLocked(count * PAGE_SIZE, page, false, false);
      ASSERT(status == ZX_OK);
    }

    if (exclusive && !is_physmap_addr(data)) {
      // unmap it from the kernel
      // NOTE: this means the image can no longer be referenced from original pointer
      status = VmAspace::kernel_aspace()->arch_aspace().Unmap(reinterpret_cast<vaddr_t>(data),
                                                              size / PAGE_SIZE, nullptr);
      ASSERT(status == ZX_OK);
    }
  }

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options,
                                          uint64_t size, fbl::RefPtr<VmObjectPaged>* obj) {
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto state = fbl::AdoptRef<VmHierarchyState>(new (&ac) VmHierarchyState);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<VmCowPages> cow_pages;
  status = VmCowPages::CreateExternal(ktl::move(src), state, size, &cow_pages);
  if (status != ZX_OK) {
    return status;
  }

  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, ktl::move(state)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // This creation has succeeded. Must wire up the cow pages and *then* place in the globals list.
  {
    Guard<Mutex> guard{&vmo->lock_};
    AssertHeld(cow_pages->lock_ref());
    cow_pages->set_paged_backlink_locked(vmo.get());
    vmo->cow_pages_ = ktl::move(cow_pages);
  }
  vmo->AddToGlobalList();

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                                            fbl::RefPtr<VmObject>* child_vmo) {
  LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

  canary_.Assert();

  // Offset must be page aligned.
  if (!IS_PAGE_ALIGNED(offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Make sure size is page aligned.
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  // Slice must be wholly contained. |size()| will read the size holding the lock. This is extra
  // acquisition is correct as we must drop the lock in order to perform the allocations.
  uint64_t our_size = this->size();
  if (!InRange(offset, size, our_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Forbid creating children of resizable VMOs. This restriction may be lifted in the future.
  if (is_resizable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t options = kSlice;
  if (is_contiguous()) {
    options |= kContiguous;
  }

  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, hierarchy_state_ptr_));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  bool notify_one_child;
  {
    Guard<Mutex> guard{&lock_};
    AssertHeld(vmo->lock_);

    // If this VMO is contiguous then we allow creating an uncached slice as we will never
    // have to perform zeroing of pages. Pages will never be zeroed since contiguous VMOs have
    // all of their pages allocated (and so COW of the zero page will never happen). The VMO is
    // also not allowed to be resizable and so will never have to allocate new pages (and zero
    // them).
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED && !is_contiguous()) {
      return ZX_ERR_BAD_STATE;
    }
    vmo->cache_policy_ = cache_policy_;

    fbl::RefPtr<VmCowPages> cow_pages;
    status = cow_pages_locked()->CreateChildSliceLocked(offset, size, &cow_pages);
    if (status != ZX_OK) {
      return status;
    }
    // Now that everything has succeeded, link up the cow pages and our parents/children.
    // Both child notification and inserting into the globals list has to happen outside the lock.
    AssertHeld(cow_pages->lock_ref());
    cow_pages->set_paged_backlink_locked(vmo.get());
    vmo->cow_pages_ = ktl::move(cow_pages);

    vmo->parent_ = this;
    notify_one_child = AddChildLocked(vmo.get());

    if (copy_name) {
      vmo->name_ = name_;
    }
    IncrementHierarchyGenerationCountLocked();
  }

  // Add to the global list now that fully initialized.
  vmo->AddToGlobalList();

  if (notify_one_child) {
    NotifyOneChild();
  }

  *child_vmo = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateClone(Resizability resizable, CloneType type, uint64_t offset,
                                       uint64_t size, bool copy_name,
                                       fbl::RefPtr<VmObject>* child_vmo) {
  LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

  canary_.Assert();

  // Copy-on-write clones of contiguous VMOs do not have meaningful semantics, so forbid them.
  if (is_contiguous()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // offset must be page aligned
  if (!IS_PAGE_ALIGNED(offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  auto options = resizable == Resizability::Resizable ? kResizable : 0u;
  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, hierarchy_state_ptr_));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  bool notify_one_child;
  {
    // Declare these prior to the guard so that any failure paths destroy these without holding
    // the lock.
    fbl::RefPtr<VmCowPages> clone_cow_pages;
    Guard<Mutex> guard{&lock_};
    AssertHeld(vmo->lock_);
    // check that we're not uncached in some way
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      return ZX_ERR_BAD_STATE;
    }

    status = cow_pages_locked()->CreateCloneLocked(type, offset, size, &clone_cow_pages);
    if (status != ZX_OK) {
      return status;
    }

    // Now that everything has succeeded we can wire up cow pages references. VMO will be placed in
    // the global list later once lock has been dropped.
    AssertHeld(clone_cow_pages->lock_ref());
    clone_cow_pages->set_paged_backlink_locked(vmo.get());
    vmo->cow_pages_ = ktl::move(clone_cow_pages);

    // Install the parent.
    vmo->parent_ = this;

    // add the new vmo as a child before we do anything, since its
    // dtor expects to find it in its parent's child list
    notify_one_child = AddChildLocked(vmo.get());

    if (copy_name) {
      vmo->name_ = name_;
    }
    IncrementHierarchyGenerationCountLocked();
  }

  // Add to the global list now that fully initialized.
  vmo->AddToGlobalList();

  if (notify_one_child) {
    NotifyOneChild();
  }

  *child_vmo = ktl::move(vmo);

  return ZX_OK;
}

void VmObjectPaged::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();

  uint64_t parent_id = 0;
  if (parent_) {
    AssertHeld(parent_->lock_ref());
    parent_id = parent_->user_id_locked();
  }

  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("vmo %p/k%" PRIu64 " ref %d parent %p/k%" PRIu64 "\n", this, user_id_, ref_count_debug(),
         parent_, parent_id);

  char name[ZX_MAX_NAME_LEN];
  get_name(name, sizeof(name));
  if (strlen(name) > 0) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    printf("name %s\n", name);
  }

  cow_pages_locked()->DumpLocked(depth, verbose);
}

size_t VmObjectPaged::AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const {
  uint64_t new_len;
  if (!TrimRange(offset, len, size_locked(), &new_len)) {
    return 0;
  }

  vmo_attribution_queries.Add(1);

  uint64_t gen_count;
  bool update_cached_attribution = false;
  // Use cached value if generation count has not changed since the last time we attributed pages.
  // Only applicable for attribution over the entire VMO, not a partial range.
  if (offset == 0 && new_len == size_locked()) {
    gen_count = GetHierarchyGenerationCountLocked();

    if (cached_page_attribution_.generation_count == gen_count) {
      vmo_attribution_cache_hits.Add(1);
      return cached_page_attribution_.page_count;
    } else {
      vmo_attribution_cache_misses.Add(1);
      update_cached_attribution = true;
    }
  }

  size_t page_count = cow_pages_locked()->AttributedPagesInRangeLocked(offset, new_len);

  if (update_cached_attribution) {
    // Cache attributed page count along with current generation count.
    DEBUG_ASSERT(cached_page_attribution_.generation_count != gen_count);
    cached_page_attribution_.generation_count = gen_count;
    cached_page_attribution_.page_count = page_count;
  }

  return page_count;
}

zx_status_t VmObjectPaged::CommitRangeInternal(uint64_t offset, uint64_t len, bool pin) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  Guard<Mutex> guard{&lock_};

  // Child slices of VMOs are currently not resizable, nor can they be made
  // from resizable parents.  If this ever changes, the logic surrounding what
  // to do if a VMO gets resized during a Commit or Pin operation will need to
  // be revisited.  Right now, we can just rely on the fact that the initial
  // vetting/trimming of the offset and length of the operation will never
  // change if the operation is being executed against a child slice.
  DEBUG_ASSERT(!is_resizable() || !is_slice());

  // Round offset and len to be page aligned.
  const uint64_t end = ROUNDUP_PAGE_SIZE(offset + len);
  DEBUG_ASSERT(end >= offset);
  offset = ROUNDDOWN(offset, PAGE_SIZE);
  len = end - offset;

  // If a pin is requested the entire range must exist and be valid,
  // otherwise we can commit a partial range.
  if (pin) {
    // If pinning we explicitly forbid zero length pins as we cannot guarantee consistent semantics.
    // For example pinning a zero length range outside the range of the VMO is an error, and so
    // pinning a zero length range inside the vmo and then resizing the VMO smaller than the pin
    // region should also be an error. To enforce this without having to have new metadata to track
    // zero length pin regions is to just forbid them. Note that the user entry points for pinning
    // already forbid zero length ranges.
    if (len == 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_locked()))) {
      return ZX_ERR_OUT_OF_RANGE;
    }
  } else {
    uint64_t new_len = len;
    if (!TrimRange(offset, len, size_locked(), &new_len)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    // was in range, just zero length
    if (new_len == 0) {
      return ZX_OK;
    }
    len = new_len;
  }

  // Should any errors occur we need to unpin everything.
  auto pin_cleanup = fbl::MakeAutoCall([this, original_offset = offset, &offset, pin]() {
    // Regardless of any resizes or other things that may have happened any pinned pages *must*
    // still be within a valid range, and so we know Unpin should succeed. The edge case is if we
    // had failed to pin *any* pages and so our original offset may be outside the current range of
    // the vmo. Additionally, as pinning a zero length range is invalid, so is unpinning, and so we
    // must avoid.
    if (pin && offset > original_offset) {
      AssertHeld(*lock());
      cow_pages_locked()->UnpinLocked(original_offset, offset - original_offset);
    }
  });

  PageRequest page_request(true);
  // As we may need to wait on arbitrary page requests we just keep running this until the commit
  // process finishes with success.
  for (;;) {
    uint64_t committed_len = 0;
    zx_status_t status =
        cow_pages_locked()->CommitRangeLocked(offset, len, &committed_len, &page_request);

    // Regardless of the return state some pages may have been committed and so unmap any pages in
    // the range we touched.
    if (committed_len > 0) {
      RangeChangeUpdateLocked(offset, committed_len, RangeChangeOp::Unmap);
    }

    // Now we can exit if we received any error states.
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      return status;
    }

    // Pin any committed range if required.
    if (pin && committed_len > 0) {
      zx_status_t status = cow_pages_locked()->PinRangeLocked(offset, committed_len);
      if (status != ZX_OK) {
        return status;
      }
    }

    // If commit was success we can stop here.
    if (status == ZX_OK) {
      DEBUG_ASSERT(committed_len == len);
      pin_cleanup.cancel();
      return ZX_OK;
    }
    DEBUG_ASSERT(status == ZX_ERR_SHOULD_WAIT);

    // Need to update how much was committed, and then wait on the page request.
    offset += committed_len;
    len -= committed_len;

    guard.CallUnlocked([&page_request, &status]() mutable { status = page_request.Wait(); });
    if (status != ZX_OK) {
      if (status == ZX_ERR_TIMED_OUT) {
        DumpLocked(0, false);
      }
      return status;
    }

    // Re-run the range checks, since size_ could have changed while we were blocked. This
    // is not a failure, since the arguments were valid when the syscall was made. It's as
    // if the commit was successful but then the pages were thrown away. Unless we are pinning,
    // in which case pages being thrown away is explicitly an error.
    if (pin) {
      // verify that the range is within the object
      if (unlikely(!InRange(offset, len, size_locked()))) {
        return ZX_ERR_OUT_OF_RANGE;
      }
    } else {
      uint64_t new_len = len;
      if (!TrimRange(offset, len, size_locked(), &new_len)) {
        return ZX_OK;
      }
      if (new_len == 0) {
        return ZX_OK;
      }
      len = new_len;
    }
  }
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);
  if (is_contiguous()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  Guard<Mutex> guard{&lock_};
  return DecommitRangeLocked(offset, len);
}

zx_status_t VmObjectPaged::DecommitRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  zx_status_t status = cow_pages_locked()->DecommitRangeLocked(offset, len);
  if (status == ZX_OK) {
    IncrementHierarchyGenerationCountLocked();
  }
  return status;
}

zx_status_t VmObjectPaged::ZeroPartialPage(uint64_t page_base_offset, uint64_t zero_start_offset,
                                           uint64_t zero_end_offset, Guard<Mutex>* guard) {
  DEBUG_ASSERT(zero_start_offset <= zero_end_offset);
  DEBUG_ASSERT(zero_end_offset <= PAGE_SIZE);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_base_offset));
  DEBUG_ASSERT(page_base_offset < size_locked());

  // TODO: Consider replacing this with a more appropriate generic API when one is available.
  if (cow_pages_locked()->PageWouldReadZeroLocked(page_base_offset)) {
    // This is already considered zero so no need to redundantly zero again.
    return ZX_OK;
  }

  // Need to actually zero out bytes in the page.
  return ReadWriteInternalLocked(
      page_base_offset + zero_start_offset, zero_end_offset - zero_start_offset, true,
      [](void* dst, size_t offset, size_t len, Guard<Mutex>* guard) -> zx_status_t {
        // We're memsetting the *kernel* address of an allocated page, so we know that this
        // cannot fault. memset may not be the most efficient, but we don't expect to be doing
        // this very often.
        memset(dst, 0, len);
        return ZX_OK;
      },
      guard);
}

zx_status_t VmObjectPaged::ZeroRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  Guard<Mutex> guard{&lock_};

  // Zeroing a range behaves as if it were an efficient zx_vmo_write. As we cannot write to uncached
  // vmo, we also cannot zero an uncahced vmo.
  if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
    return ZX_ERR_BAD_STATE;
  }

  // Trim the size and validate it is in range of the vmo.
  uint64_t new_len;
  if (!TrimRange(offset, len, size_locked(), &new_len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Construct our initial range. Already checked the range above so we know it cannot overflow.
  uint64_t start = offset;
  uint64_t end = start + new_len;

  // Helper that checks and establishes our invariants. We use this after calling functions that
  // may have temporarily released the lock.
  auto establish_invariants = [this, end]() TA_REQ(lock_) {
    if (end > size_locked()) {
      return ZX_ERR_BAD_STATE;
    }
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
  };

  uint64_t start_page_base = ROUNDDOWN(start, PAGE_SIZE);
  uint64_t end_page_base = ROUNDDOWN(end, PAGE_SIZE);

  if (unlikely(start_page_base != start)) {
    // Need to handle the case were end is unaligned and on the same page as start
    if (unlikely(start_page_base == end_page_base)) {
      return ZeroPartialPage(start_page_base, start - start_page_base, end - start_page_base,
                             &guard);
    }
    zx_status_t status =
        ZeroPartialPage(start_page_base, start - start_page_base, PAGE_SIZE, &guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    start = start_page_base + PAGE_SIZE;
  }

  if (unlikely(end_page_base != end)) {
    zx_status_t status = ZeroPartialPage(end_page_base, 0, end - end_page_base, &guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    end = end_page_base;
  }

  // Now that we have a page aligned range we can try hand over to the cow pages zero method.
  // Always increment the gen count as it's possible for ZeroPagesLocked to fail part way through
  // and it doesn't unroll its actions.
  IncrementHierarchyGenerationCountLocked();

  return cow_pages_locked()->ZeroPagesLocked(start, end);
}

zx_status_t VmObjectPaged::Resize(uint64_t s) {
  canary_.Assert();

  LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

  if (!is_resizable()) {
    return ZX_ERR_UNAVAILABLE;
  }

  // round up the size to the next page size boundary and make sure we don't wrap
  zx_status_t status = RoundSize(s, &s);
  if (status != ZX_OK) {
    return status;
  }

  Guard<Mutex> guard{&lock_};

  status = cow_pages_locked()->ResizeLocked(s);
  if (status != ZX_OK) {
    return status;
  }
  IncrementHierarchyGenerationCountLocked();
  return ZX_OK;
}

// perform some sort of copy in/out on a range of the object using a passed in lambda
// for the copy routine. The copy routine has the expected type signature of:
// (uint64_t src_offset, uint64_t dest_offset, bool write, Guard<Mutex> *guard) -> zx_status_t
// The passed in guard may have its CallUnlocked member used, but if it does then ZX_OK must not be
// the return value. A return of ZX_ERR_SHOULD_WAIT implies that the attempted copy should be tried
// again at the exact same offsets.
template <typename T>
zx_status_t VmObjectPaged::ReadWriteInternalLocked(uint64_t offset, size_t len, bool write,
                                                   T copyfunc, Guard<Mutex>* guard) {
  canary_.Assert();

  uint64_t end_offset;
  if (add_overflow(offset, len, &end_offset)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Declare a lambda that will check any object properties we require to be true. We place these
  // in a lambda so that we can perform them any time the lock is dropped.
  auto check = [this, &end_offset]() -> zx_status_t {
    AssertHeld(lock_);
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      return ZX_ERR_BAD_STATE;
    }
    if (end_offset > size_locked()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
  };

  // Perform initial check.
  if (zx_status_t status = check(); status != ZX_OK) {
    return status;
  }

  // Track our two offsets.
  uint64_t src_offset = offset;
  size_t dest_offset = 0;
  while (len > 0) {
    const size_t page_offset = src_offset % PAGE_SIZE;
    const size_t tocopy = ktl::min(PAGE_SIZE - page_offset, len);

    // fault in the page
    PageRequest page_request;
    paddr_t pa;
    zx_status_t status =
        GetPageLocked(src_offset, VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0), nullptr,
                      &page_request, nullptr, &pa);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // Must block on asynchronous page requests whilst not holding the lock.
      guard->CallUnlocked([&status, &page_request]() { status = page_request.Wait(); });
      if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
          DumpLocked(0, false);
        }
        return status;
      }
      // Recheck properties and if all is good go back to the top of the loop to attempt to fault in
      // the page again.
      status = check();
      if (status == ZX_OK) {
        continue;
      }
    }
    if (status != ZX_OK) {
      return status;
    }
    // Compute the kernel mapping of this page.
    char* page_ptr = reinterpret_cast<char*>(paddr_to_physmap(pa));

    // Call the copy routine. If the copy was successful then ZX_OK is returned, otherwise
    // ZX_ERR_SHOULD_WAIT may be returned to indicate the copy failed but we can retry it.
    status = copyfunc(page_ptr + page_offset, dest_offset, tocopy, guard);

    if (status == ZX_ERR_SHOULD_WAIT) {
      // Recheck properties. If all is good we cannot simply retry the copy as the underlying page
      // could have changed, so we retry the loop from the top.
      status = check();
      if (status == ZX_OK) {
        continue;
      }
    }
    if (status != ZX_OK) {
      return status;
    }

    // Advance the copy location.
    src_offset += tocopy;
    dest_offset += tocopy;
    len -= tocopy;
  }

  return ZX_OK;
}

zx_status_t VmObjectPaged::Read(void* _ptr, uint64_t offset, size_t len) {
  canary_.Assert();
  // test to make sure this is a kernel pointer
  if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
    DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // read routine that just uses a memcpy
  char* ptr = reinterpret_cast<char*>(_ptr);
  auto read_routine = [ptr](const void* src, size_t offset, size_t len,
                            Guard<Mutex>* guard) -> zx_status_t {
    memcpy(ptr + offset, src, len);
    return ZX_OK;
  };

  Guard<Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, false, read_routine, &guard);
}

zx_status_t VmObjectPaged::Write(const void* _ptr, uint64_t offset, size_t len) {
  canary_.Assert();
  // test to make sure this is a kernel pointer
  if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
    DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // write routine that just uses a memcpy
  const char* ptr = reinterpret_cast<const char*>(_ptr);
  auto write_routine = [ptr](void* dst, size_t offset, size_t len,
                             Guard<Mutex>* guard) -> zx_status_t {
    memcpy(dst, ptr + offset, len);
    return ZX_OK;
  };

  Guard<Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::Lookup(
    uint64_t offset, uint64_t len,
    fbl::Function<zx_status_t(uint64_t offset, paddr_t pa)> lookup_fn) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  return cow_pages_locked()->LookupLocked(offset, len, ktl::move(lookup_fn));
}

zx_status_t VmObjectPaged::LookupContiguous(uint64_t offset, uint64_t len, paddr_t* out_paddr) {
  canary_.Assert();

  if (unlikely(len == 0 || !IS_PAGE_ALIGNED(offset))) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  if (unlikely(!InRange(offset, len, size_locked()))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (unlikely(is_contiguous())) {
    // Already checked that the entire requested range is valid, and since we know all our pages are
    // contiguous we can simply lookup one page.
    len = PAGE_SIZE;
  } else if (unlikely(len != PAGE_SIZE)) {
    // Multi-page lookup only supported for contiguous VMOs.
    return ZX_ERR_BAD_STATE;
  }

  // Lookup the one page / first page of contiguous VMOs.
  paddr_t paddr = 0;
  zx_status_t status =
      cow_pages_locked()->LookupLocked(offset, len, [&paddr](uint64_t offset, paddr_t pa) {
        paddr = pa;
        return ZX_ERR_STOP;
      });
  if (status != ZX_OK) {
    return status;
  }
  if (paddr == 0) {
    return ZX_ERR_NO_MEMORY;
  }
  if (out_paddr) {
    *out_paddr = paddr;
  }
  return ZX_OK;
}

zx_status_t VmObjectPaged::ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr,
                                    uint64_t offset, size_t len) {
  canary_.Assert();

  // read routine that uses copy_to_user
  auto read_routine = [ptr, current_aspace](const char* src, size_t offset, size_t len,
                                            Guard<Mutex>* guard) -> zx_status_t {
    auto copy_result = ptr.byte_offset(offset).copy_array_to_user_capture_faults(src, len);

    // If a fault has actually occurred, then we will have captured fault info that we can use to
    // handle the fault.
    if (copy_result.fault_info.has_value()) {
      zx_status_t result;
      guard->CallUnlocked([&info = *copy_result.fault_info, &result, current_aspace] {
        result = current_aspace->SoftFault(info.pf_va, info.pf_flags);
      });
      // If we handled the fault, tell the upper level to try again.
      return result == ZX_OK ? ZX_ERR_SHOULD_WAIT : result;
    }

    // If we encounter _any_ unrecoverable error from the copy operation which
    // produced no fault address, squash the error down to just "NOT_FOUND".
    // This is what the SoftFault error would have told us if we did try to
    // handle the fault and could not.
    return copy_result.status == ZX_OK ? ZX_OK : ZX_ERR_NOT_FOUND;
  };

  Guard<Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, false, read_routine, &guard);
}

zx_status_t VmObjectPaged::WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr,
                                     uint64_t offset, size_t len) {
  canary_.Assert();

  // write routine that uses copy_from_user
  auto write_routine = [ptr, &current_aspace](char* dst, size_t offset, size_t len,
                                              Guard<Mutex>* guard) -> zx_status_t {
    auto copy_result = ptr.byte_offset(offset).copy_array_from_user_capture_faults(dst, len);

    // If a fault has actually occurred, then we will have captured fault info that we can use to
    // handle the fault.
    if (copy_result.fault_info.has_value()) {
      zx_status_t result;
      guard->CallUnlocked([&info = *copy_result.fault_info, &result, current_aspace] {
        result = current_aspace->SoftFault(info.pf_va, info.pf_flags);
      });
      // If we handled the fault, tell the upper level to try again.
      return result == ZX_OK ? ZX_ERR_SHOULD_WAIT : result;
    }

    // If we encounter _any_ unrecoverable error from the copy operation which
    // produced no fault address, squash the error down to just "NOT_FOUND".
    // This is what the SoftFault error would have told us if we did try to
    // handle the fault and could not.
    return copy_result.status == ZX_OK ? ZX_OK : ZX_ERR_NOT_FOUND;
  };

  Guard<Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  canary_.Assert();

  Guard<Mutex> src_guard{&lock_};

  // This is only used by the userpager API, which has significant restrictions on
  // what sorts of vmos are acceptable. If splice starts being used in more places,
  // then this restriction might need to be lifted.
  // TODO: Check that the region is locked once locking is implemented
  if (mapping_list_len_ || children_list_len_) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = cow_pages_locked()->TakePagesLocked(offset, len, pages);

  if (status == ZX_OK) {
    IncrementHierarchyGenerationCountLocked();
  }
  return status;
}

zx_status_t VmObjectPaged::SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  canary_.Assert();

  Guard<Mutex> guard{&lock_};

  // It is possible that supply pages fails and we increment the gen count needlessly, but the user
  // is certainly expecting it to succeed.
  IncrementHierarchyGenerationCountLocked();

  return cow_pages_locked()->SupplyPagesLocked(offset, len, pages);
}

zx_status_t VmObjectPaged::SetMappingCachePolicy(const uint32_t cache_policy) {
  // Is it a valid cache flag?
  if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  // conditions for allowing the cache policy to be set:
  // 1) vmo either has no pages committed currently or is transitioning from being cached
  // 2) vmo has no pinned pages
  // 3) vmo has no mappings
  // 4) vmo has no children
  // 5) vmo is not a child
  // Counting attributed pages does a sufficient job of checking for committed pages since we also
  // require no children and no parent, so attribution == precisely our pages.
  if (cow_pages_locked()->AttributedPagesInRangeLocked(0, size_locked()) != 0 &&
      cache_policy_ != ARCH_MMU_FLAG_CACHED) {
    // We forbid to transitioning committed pages from any kind of uncached->cached policy as we do
    // not currently have a story for dealing with the speculative loads that may have happened
    // against the cached physmap. That is, whilst a page was uncached the cached physmap version
    // may have been loaded and sitting in cache. If we switch to cached mappings we may then use
    // stale data out of the cache.
    // This isn't a problem if going *from* an cached state, as we can safely clean+invalidate.
    // Similarly it's not a problem if there aren't actually any committed pages.
    return ZX_ERR_BAD_STATE;
  }
  // If we are contiguous we 'pre pinned' all the pages, but this doesn't count for pinning as far
  // as the user and potential DMA is concerned. Take this into account when checking if the user
  // pinned any pages.
  uint64_t expected_pin_count = (is_contiguous() ? (size_locked() / PAGE_SIZE) : 0);
  if (cow_pages_locked()->pinned_page_count_locked() > expected_pin_count) {
    return ZX_ERR_BAD_STATE;
  }
  if (!mapping_list_.is_empty()) {
    return ZX_ERR_BAD_STATE;
  }
  if (!children_list_.is_empty()) {
    return ZX_ERR_BAD_STATE;
  }
  if (parent_) {
    return ZX_ERR_BAD_STATE;
  }

  // If transitioning from a cached policy we must clean/invalidate all the pages as the kernel may
  // have written to them on behalf of the user.
  if (cache_policy_ == ARCH_MMU_FLAG_CACHED && cache_policy != ARCH_MMU_FLAG_CACHED) {
    zx_status_t status =
        cow_pages_locked()->LookupLocked(0, size_locked(), [](uint64_t offset, paddr_t pa) {
          arch_clean_invalidate_cache_range((vaddr_t)paddr_to_physmap(pa), PAGE_SIZE);
          return ZX_ERR_NEXT;
        });
    if (status != ZX_OK) {
      return status;
    }
  }

  cache_policy_ = cache_policy;

  return ZX_OK;
}

void VmObjectPaged::RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) {
  canary_.Assert();

  // offsets for vmos needn't be aligned, but vmars use aligned offsets
  const uint64_t aligned_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t aligned_len = ROUNDUP(offset + len, PAGE_SIZE) - aligned_offset;

  for (auto& m : mapping_list_) {
    AssertHeld(*m.object_lock());
    if (op == RangeChangeOp::Unmap) {
      m.AspaceUnmapVmoRangeLocked(aligned_offset, aligned_len);
    } else if (op == RangeChangeOp::RemoveWrite) {
      m.AspaceRemoveWriteVmoRangeLocked(aligned_offset, aligned_len);
    } else {
      panic("Unknown RangeChangeOp %d\n", static_cast<int>(op));
    }
  }
}
