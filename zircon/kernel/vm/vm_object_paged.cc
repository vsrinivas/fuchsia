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
#include <lib/fit/defer.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/move.h>
#include <vm/bootreserve.h>
#include <vm/fault.h>
#include <vm/page_source.h>
#include <vm/physical_page_provider.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_cow_pages.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

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

  if (options_ & kAlwaysPinned) {
    Unpin(0, size());
  }

  Guard<CriticalMutex> guard{&lock_};

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

zx_status_t VmObjectPaged::HintRange(uint64_t offset, uint64_t len, EvictionHint hint) {
  canary_.Assert();

  uint64_t end_offset;
  if (add_overflow(offset, len, &end_offset)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (can_block_on_page_requests() && hint == EvictionHint::AlwaysNeed) {
    lockdep::AssertNoLocksHeld();
  }

  Guard<CriticalMutex> guard{lock()};

  // Ignore hints for non user-pager-backed VMOs. We choose to silently ignore hints for
  // incompatible combinations instead of failing. This is because the kernel does not make any
  // explicit guarantees on hints; since they are just hints, the kernel is always free to ignore
  // them.
  if (!cow_pages_locked()->can_root_source_evict_locked()) {
    return ZX_OK;
  }

  if (!InRange(offset, len, size_locked())) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  switch (hint) {
    case EvictionHint::DontNeed: {
      cow_pages_locked()->PromoteRangeForReclamationLocked(offset, len);
      break;
    }
    case EvictionHint::AlwaysNeed: {
      cow_pages_locked()->ProtectRangeFromReclamationLocked(offset, len, &guard);
      break;
    }
  }

  return ZX_OK;
}

bool VmObjectPaged::CanDedupZeroPagesLocked() {
  canary_.Assert();

  // Skip uncached VMOs as we cannot efficiently scan them.
  if ((cache_policy_ & ZX_CACHE_POLICY_MASK) != ZX_CACHE_POLICY_CACHED) {
    return false;
  }

  // Okay to dedup from this VMO.
  return true;
}

zx_status_t VmObjectPaged::CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                        fbl::RefPtr<VmObjectPaged>* obj) {
  DEBUG_ASSERT(!(options & (kContiguous | kCanBlockOnPageRequests)));

  // Cannot be resizable and pinned, otherwise we will lose track of the pinned range.
  if ((options & kResizable) && (options & kAlwaysPinned)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (pmm_alloc_flags & PMM_ALLOC_FLAG_CAN_WAIT) {
    options |= kCanBlockOnPageRequests;
  }

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
  status = VmCowPages::Create(state, VmCowPagesOptions::kNone, pmm_alloc_flags, size, &cow_pages);
  if (status != ZX_OK) {
    return status;
  }

  // If this VMO will always be pinned, allocate and pin the pages in the VmCowPages prior to
  // creating the VmObjectPaged. This ensures the VmObjectPaged destructor can assume that the pages
  // are committed and pinned.
  if (options & kAlwaysPinned) {
    list_node_t prealloc_pages;
    list_initialize(&prealloc_pages);
    status = pmm_alloc_pages(size / PAGE_SIZE, pmm_alloc_flags, &prealloc_pages);
    if (status != ZX_OK) {
      return status;
    }
    Guard<CriticalMutex> guard{cow_pages->lock()};
    // Add all the preallocated pages to the object, this takes ownership of all pages regardless
    // of the outcome. This is a new VMO, but this call could fail due to OOM.
    status = cow_pages->AddNewPagesLocked(0, &prealloc_pages, VmCowPages::CanOverwriteContent::Zero,
                                          true, false);
    if (status != ZX_OK) {
      return status;
    }
    // With all the pages in place, pin them.
    status = cow_pages->PinRangeLocked(0, size);
    ASSERT(status == ZX_OK);
  }

  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, ktl::move(state)));
  if (!ac.check()) {
    if (options & kAlwaysPinned) {
      Guard<CriticalMutex> guard{cow_pages->lock()};
      cow_pages->UnpinLocked(0, size, false);
    }
    return ZX_ERR_NO_MEMORY;
  }

  // This creation has succeeded. Must wire up the cow pages and *then* place in the globals list.
  {
    Guard<CriticalMutex> guard{&vmo->lock_};
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
  if (options & (kContiguous | kCanBlockOnPageRequests)) {
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

  fbl::AllocChecker ac;
  // For contiguous VMOs, we need a PhysicalPageProvider to reclaim specific loaned physical pages
  // on commit.
  auto page_provider = fbl::AdoptRef(new (&ac) PhysicalPageProvider(size));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  PhysicalPageProvider* physical_page_provider_ptr = page_provider.get();
  fbl::RefPtr<PageSource> page_source =
      fbl::AdoptRef(new (&ac) PageSource(ktl::move(page_provider)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* page_source_ptr = page_source.get();

  fbl::RefPtr<VmObjectPaged> vmo;
  status = CreateWithSourceCommon(ktl::move(page_source), pmm_alloc_flags, kContiguous, size, &vmo);
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
  Guard<CriticalMutex> guard{&vmo->lock_};
  // Add them to the appropriate range of the object, this takes ownership of all the pages
  // regardless of outcome.
  // This is a newly created VMO with a page source, so we don't expect to be overwriting anything
  // in its page list.
  status = vmo->cow_pages_locked()->AddNewPagesLocked(0, &page_list,
                                                      VmCowPages::CanOverwriteContent::None);
  if (status != ZX_OK) {
    return status;
  }

  physical_page_provider_ptr->Init(vmo->cow_pages_locked(), page_source_ptr, pa);

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

    Guard<CriticalMutex> guard{&vmo->lock_};

    for (size_t count = 0; count < size / PAGE_SIZE; count++) {
      paddr_t pa = start_paddr + count * PAGE_SIZE;
      vm_page_t* page = paddr_to_vm_page(pa);
      ASSERT(page);

      if (page->state() == vm_page_state::WIRED) {
        boot_reserve_unwire_page(page);
      } else {
        // This function is only valid for memory in the boot image,
        // which should all be wired.
        panic("page used to back static vmo in unusable state: paddr %#" PRIxPTR " state %zu\n", pa,
              VmPageStateIndex(page->state()));
      }
      // This is a newly created anonymous VMO, so we expect to be overwriting zeros. A newly
      // created anonymous VMO with no committed pages has all its content implicitly zero.
      status = vmo->cow_pages_locked()->AddNewPageLocked(
          count * PAGE_SIZE, page, VmCowPages::CanOverwriteContent::Zero, nullptr, false, false);
      ASSERT(status == ZX_OK);
      DEBUG_ASSERT(!page->is_loaned());
    }

    if (exclusive && !is_physmap_addr(data)) {
      // unmap it from the kernel
      // NOTE: this means the image can no longer be referenced from original pointer
      status = VmAspace::kernel_aspace()->arch_aspace().Unmap(
          reinterpret_cast<vaddr_t>(data), size / PAGE_SIZE, ArchVmAspace::EnlargeOperation::No,
          nullptr);
      ASSERT(status == ZX_OK);
    }
    if (!exclusive) {
      // Pin all the pages as we must never decommit any of them since they are shared elsewhere.
      status = vmo->cow_pages_locked()->PinRangeLocked(0, size);
      ASSERT(status == ZX_OK);
    }
  }

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options,
                                          uint64_t size, fbl::RefPtr<VmObjectPaged>* obj) {
  if (options & (kDiscardable | kCanBlockOnPageRequests | kAlwaysPinned)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  // External VMOs always support delayed PMM allocations, since they already have to tolerate
  // arbitrary waits for pages due to the PageSource.
  return CreateWithSourceCommon(ktl::move(src), PMM_ALLOC_FLAG_ANY | PMM_ALLOC_FLAG_CAN_WAIT,
                                options | kCanBlockOnPageRequests, size, obj);
}

zx_status_t VmObjectPaged::CreateWithSourceCommon(fbl::RefPtr<PageSource> src,
                                                  uint32_t pmm_alloc_flags, uint32_t options,
                                                  uint64_t size, fbl::RefPtr<VmObjectPaged>* obj) {
  // Caller must check that size is page aligned.
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(!(options & kAlwaysPinned));

  fbl::AllocChecker ac;
  auto state = fbl::AdoptRef<VmHierarchyState>(new (&ac) VmHierarchyState);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // The cow pages will have a page source, so blocking is always possible.
  options |= kCanBlockOnPageRequests;

  VmCowPagesOptions cow_options = VmCowPagesOptions::kNone;
  if (options & kContiguous) {
    cow_options |= VmCowPagesOptions::kCannotDecommitZeroPages;
  }
  fbl::RefPtr<VmCowPages> cow_pages;
  zx_status_t status =
      VmCowPages::CreateExternal(ktl::move(src), cow_options, state, size, &cow_pages);
  if (status != ZX_OK) {
    return status;
  }

  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, ktl::move(state)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // This creation has succeeded. Must wire up the cow pages and *then* place in the globals list.
  {
    Guard<CriticalMutex> guard{&vmo->lock_};
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

  if (can_block_on_page_requests()) {
    options |= kCanBlockOnPageRequests;
  }

  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(options, hierarchy_state_ptr_));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  bool notify_one_child;
  {
    Guard<CriticalMutex> guard{&lock_};
    AssertHeld(vmo->lock_);

    // If this VMO is contiguous then we allow creating an uncached slice.  When zeroing pages that
    // are reclaimed from having been loaned from a contiguous VMO, we will zero the pages and flush
    // the zeroes to RAM.
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

  uint32_t options = 0;
  if (resizable == Resizability::Resizable) {
    options |= kResizable;
  }
  if (can_block_on_page_requests()) {
    options |= kCanBlockOnPageRequests;
  }
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
    Guard<CriticalMutex> guard{&lock_};
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

VmObject::AttributionCounts VmObjectPaged::AttributedPagesInRangeLocked(uint64_t offset,
                                                                        uint64_t len) const {
  uint64_t new_len;
  if (!TrimRange(offset, len, size_locked(), &new_len)) {
    return AttributionCounts{};
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
      return cached_page_attribution_.page_counts;
    } else {
      vmo_attribution_cache_misses.Add(1);
      update_cached_attribution = true;
    }
  }

  AttributionCounts page_counts = cow_pages_locked()->AttributedPagesInRangeLocked(offset, new_len);

  if (update_cached_attribution) {
    // Cache attributed page count along with current generation count.
    DEBUG_ASSERT(cached_page_attribution_.generation_count != gen_count);
    cached_page_attribution_.generation_count = gen_count;
    cached_page_attribution_.page_counts = page_counts;
  }

  return page_counts;
}

zx_status_t VmObjectPaged::CommitRangeInternal(uint64_t offset, uint64_t len, bool pin,
                                               bool write) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }

  // We only expect write to be set if this a pin. All non-pin commits are reads.
  DEBUG_ASSERT(!write || pin);

  Guard<CriticalMutex> guard{&lock_};

  // Child slices of VMOs are currently not resizable, nor can they be made
  // from resizable parents.  If this ever changes, the logic surrounding what
  // to do if a VMO gets resized during a Commit or Pin operation will need to
  // be revisited.  Right now, we can just rely on the fact that the initial
  // vetting/trimming of the offset and length of the operation will never
  // change if the operation is being executed against a child slice.
  DEBUG_ASSERT(!is_resizable() || !is_slice());

  // Round offset and len to be page aligned. Use a sub-scope to validate that temporary end
  // calculations cannot be accidentally used later on.
  {
    uint64_t end;
    if (add_overflow(offset, len, &end)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    const uint64_t end_page = ROUNDUP_PAGE_SIZE(end);
    if (end_page < end) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    DEBUG_ASSERT(end_page >= offset);
    offset = ROUNDDOWN(offset, PAGE_SIZE);
    len = end_page - offset;
  }

  // If a pin is requested the entire range must exist and be valid,
  // otherwise we can commit a partial range.
  if (pin) {
    // If pinning we explicitly forbid zero length pins as we cannot guarantee consistent semantics.
    // For example pinning a zero length range outside the range of the VMO is an error, and so
    // pinning a zero length range inside the vmo and then resizing the VMO smaller than the pin
    // region should also be an error. To enforce this without having to have new metadata to track
    // zero length pin regions is to just forbid them. Note that the user entry points for pinning
    // already forbid zero length ranges.
    if (unlikely(len == 0)) {
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

  // Tracks the end of the pinned range to unpin in case of failure. The |offset| might lag behind
  // the pinned range, as it tracks the range that has been completely processed, which would
  // also include dirtying the page after pinning in case of a write.
  uint64_t pinned_end_offset = offset;
  // Should any errors occur we need to unpin everything. If we were asked to write, we need to mark
  // the VMO modified if any pages were committed.
  auto deferred_cleanup =
      fit::defer([this, original_offset = offset, &offset, &len, &pinned_end_offset, pin, write]() {
        AssertHeld(lock_);
        // If we were not able to pin the entire range, i.e. len is not 0, we need to unpin
        // everything. Regardless of any resizes or other things that may have happened any pinned
        // pages *must* still be within a valid range, and so we know Unpin should succeed. The edge
        // case is if we had failed to pin *any* pages and so our original offset may be outside the
        // current range of the vmo. Additionally, as pinning a zero length range is invalid, so is
        // unpinning, and so we must avoid.
        if (pin && len > 0 && pinned_end_offset > original_offset) {
          cow_pages_locked()->UnpinLocked(original_offset, pinned_end_offset - original_offset,
                                          /*allow_gaps=*/false);
        } else if (write && offset > original_offset) {
          // Mark modified as we successfully committed pages for writing *and* we did not end up
          // undoing a partial pin (the if-block above).
          mark_modified_locked();
        }
      });

  __UNINITIALIZED LazyPageRequest page_request(true);
  // Convenience lambda to advance offset by processed_len, indicating that all pages in the range
  // [offset, offset + processed_len) have been processed, then potentially wait on the page_request
  // (if wait_on_page_request is set to true), and revalidate range checks after waiting.
  auto advance_processed_range = [&](uint64_t processed_len,
                                     bool wait_on_page_request) -> zx_status_t {
    offset += processed_len;
    len -= processed_len;

    if (wait_on_page_request) {
      DEBUG_ASSERT(can_block_on_page_requests());
      zx_status_t wait_status = ZX_OK;
      AssertHeld(lock_);
      guard.CallUnlocked(
          [&page_request, &wait_status]() mutable { wait_status = page_request->Wait(); });
      if (wait_status != ZX_OK) {
        if (wait_status == ZX_ERR_TIMED_OUT) {
          DumpLocked(0, false);
        }
        return wait_status;
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
          // No remaining range to process. Set len to 0 so that the top level loop can exit.
          len = 0;
          return ZX_OK;
        }
        len = new_len;
      }
    }
    return ZX_OK;
  };

  // As we may need to wait on arbitrary page requests we just keep running this as long as there is
  // a non-zero range to process.
  while (len > 0) {
    uint64_t committed_len = 0;
    zx_status_t commit_status =
        cow_pages_locked()->CommitRangeLocked(offset, len, &committed_len, &page_request);
    DEBUG_ASSERT(committed_len <= len);

    // Now we can exit if we received any error states.
    if (commit_status != ZX_OK && commit_status != ZX_ERR_SHOULD_WAIT) {
      return commit_status;
    }

    // If we're required to pin, try to pin the committed range before waiting on the page_request,
    // which has been populated to request pages beyond the committed range.
    // Even though the page_request has already been initialized, we choose to first completely
    // process the committed range, which could end up canceling the already initialized page
    // request. This allows us to keep making forward progress as we will potentially pin a few
    // pages before trying to fault in further pages, thereby preventing the already committed (and
    // pinned) pages from being evicted while we wait with the lock dropped.
    if (pin && committed_len > 0) {
      // We need to replace any loaned pages in the committed range with non-loaned pages first,
      // since pinning expects all pages to be non-loaned. Replacing loaned pages requires a page
      // request too. At any time we'll only be able to wait on a single page request, and after the
      // wait the conditions that resulted in the previous request might have changed, so we can
      // just cancel and reuse the existing page_request.
      page_request->CancelRequest();

      uint64_t non_loaned_len = 0;
      zx_status_t replace_status = cow_pages_locked()->ReplacePagesWithNonLoanedLocked(
          offset, committed_len, &page_request, &non_loaned_len);
      DEBUG_ASSERT(non_loaned_len <= committed_len);
      if (replace_status == ZX_OK) {
        DEBUG_ASSERT(non_loaned_len == committed_len);
      } else if (replace_status != ZX_ERR_SHOULD_WAIT) {
        return replace_status;
      }

      // We can safely pin the non-loaned range before waiting on the page request.
      if (non_loaned_len > 0) {
        // Verify that we are starting the pin after the previously pinned range, as we do not want
        // to repeatedly pin the same pages.
        ASSERT(pinned_end_offset == offset);
        zx_status_t pin_status = cow_pages_locked()->PinRangeLocked(offset, non_loaned_len);
        if (pin_status != ZX_OK) {
          return pin_status;
        }
      }
      // At this point we have successfully committed and pinned non_loaned_len.
      uint64_t pinned_len = non_loaned_len;
      pinned_end_offset = offset + pinned_len;

      // If this is a write and the VMO supports dirty tracking, we also need to mark the pinned
      // pages Dirty.
      // We pin the pages first before marking them dirty in order to guarantee forward progress.
      // Pinning the pages will prevent them from getting decommitted while we are waiting on the
      // dirty page request without the lock held.
      if (write && pinned_len > 0 && is_dirty_tracked_locked()) {
        // Prepare the committed range for writing. We need a page request for this too, so cancel
        // any existing one and reuse it.
        page_request->CancelRequest();

        // We want to dirty the entire pinned range.
        uint64_t to_dirty_len = pinned_len;
        while (to_dirty_len > 0) {
          uint64_t dirty_len = 0;
          zx_status_t write_status = cow_pages_locked()->PrepareForWriteLocked(
              offset, to_dirty_len, &page_request, &dirty_len);
          DEBUG_ASSERT(dirty_len <= to_dirty_len);
          if (write_status != ZX_OK && write_status != ZX_ERR_SHOULD_WAIT) {
            return write_status;
          }
          // Account for the pages that were dirtied during this attempt.
          to_dirty_len -= dirty_len;

          // At this point we have successfully committed, pinned, and dirtied dirty_len. This is
          // where we need to restart the next call to PrepareForWriteLocked. Advance the offset to
          // reflect that, and then wait on the page request beyond dirty_len (if any).
          zx_status_t wait_status = advance_processed_range(
              dirty_len, /*wait_on_page_request=*/write_status == ZX_ERR_SHOULD_WAIT);
          if (wait_status != ZX_OK) {
            return wait_status;
          }
          // Retry dirtying pages beyond dirty_len. Note that it is fine to resume the inner loop
          // here and directly call PrepareForWriteLocked after advancing the offset because the
          // pages were pinned previously, and so they could not have gotten decommitted while we
          // waited on the page request.
          if (write_status == ZX_ERR_SHOULD_WAIT) {
            // Resume the loop that repeatedly calls PrepareForWriteLocked until all the pinned
            // pages have been marked dirty.
            continue;
          }
        }
      } else {
        // We did not need to perform any dirty tracking. So we can advance the offset over the
        // pinned length. Now that we've dealt with all the pages in the non-loaned range, wait on
        // the page request for offsets beyond (if any).
        zx_status_t wait_status = advance_processed_range(
            pinned_len, /*wait_on_page_request=*/replace_status == ZX_ERR_SHOULD_WAIT);
        if (wait_status != ZX_OK) {
          return wait_status;
        }
        // Since we dropped the lock while waiting, things might have changed, so reattempt
        // committing beyond the length we had successfully pinned before waiting.
        if (replace_status == ZX_ERR_SHOULD_WAIT) {
          continue;
        }
      }
    } else {
      // We were either not required to pin, or committed_len was 0. We need to update how much was
      // committed, and then wait on the page request (if any).
      zx_status_t wait_status = advance_processed_range(
          committed_len, /*wait_on_page_request=*/commit_status == ZX_ERR_SHOULD_WAIT);
      if (wait_status != ZX_OK) {
        return wait_status;
      }
      // After we're done waiting on the page request, we loop around with the same |offset| and
      // |len|, so that we can reprocess the range populated by the page request, with another
      // call to VmCowPages::CommitRangeLocked(). This is required to make any COW copies of pages
      // that were just supplied.
      // - The first call to VmCowPages::CommitRangeLocked() returns early from
      // LookupPagesLocked() with ZX_ERR_SHOULD_WAIT after queueing a page request for the absent
      // page.
      // - The second call to VmCowPages::CommitRangeLocked() calls LookupPagesLocked() which
      // copies out the now present page (if required).
      if (commit_status == ZX_ERR_SHOULD_WAIT) {
        continue;
      }
    }

    // If commit was successful we should have no more to process.
    DEBUG_ASSERT(commit_status != ZX_OK || len == 0);
  }
  return ZX_OK;
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);
  Guard<CriticalMutex> guard{&lock_};
  if (is_contiguous() && !pmm_physical_page_borrowing_config()->is_loaning_enabled()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return DecommitRangeLocked(offset, len);
}

zx_status_t VmObjectPaged::DecommitRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // Decommit of pages from a contiguous VMO relies on contiguous VMOs not being resizable.
  DEBUG_ASSERT(!is_resizable() || !is_contiguous());

  zx_status_t status = cow_pages_locked()->DecommitRangeLocked(offset, len);
  if (status == ZX_OK) {
    IncrementHierarchyGenerationCountLocked();
  }
  return status;
}

zx_status_t VmObjectPaged::ZeroPartialPageLocked(uint64_t page_base_offset,
                                                 uint64_t zero_start_offset,
                                                 uint64_t zero_end_offset,
                                                 Guard<CriticalMutex>* guard) {
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
      [](void* dst, size_t offset, size_t len, Guard<CriticalMutex>* guard) -> zx_status_t {
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
  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }
  Guard<CriticalMutex> guard{&lock_};

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
  auto establish_invariants = [this, &end]() TA_REQ(lock_) {
    if (end > size_locked()) {
      return ZX_ERR_OUT_OF_RANGE;
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
      return ZeroPartialPageLocked(start_page_base, start - start_page_base, end - start_page_base,
                                   &guard);
    }
    zx_status_t status =
        ZeroPartialPageLocked(start_page_base, start - start_page_base, PAGE_SIZE, &guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    start = start_page_base + PAGE_SIZE;
  }

  if (unlikely(end_page_base != end)) {
    zx_status_t status = ZeroPartialPageLocked(end_page_base, 0, end - end_page_base, &guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    end = end_page_base;
  }

  // Now that we have a page aligned range we can try hand over to the cow pages zero method.
  // Increment the gen count as it's possible for ZeroPagesLocked to fail part way through
  // and it doesn't unroll its actions.
  //
  // Zeroing pages of a contiguous VMO doesn't commit or de-commit any pages currently, but we
  // increment the generation count anyway in case that changes in future, and to keep the tests
  // more consistent.
  IncrementHierarchyGenerationCountLocked();

  // Currently we want ZeroPagesLocked() to not decommit any pages from a contiguous VMO.  In debug
  // we can assert that (not a super fast assert, but seems worthwhile; it's debug only).
#if DEBUG_ASSERT_IMPLEMENTED
  uint64_t page_count_before = is_contiguous() ? cow_pages_locked()->DebugGetPageCountLocked() : 0;
#endif

  auto mark_modified = fit::defer([this, original_start = start, &start]() {
    if (start > original_start) {
      // Mark modified since we wrote zeros.
      AssertHeld(lock_);
      mark_modified_locked();
    }
  });

  // We might need a page request if the VMO is backed by a page source.
  __UNINITIALIZED LazyPageRequest page_request;
  while (start < end) {
    uint64_t zeroed_len = 0;
    zx_status_t status =
        cow_pages_locked()->ZeroPagesLocked(start, end, &page_request, &zeroed_len);
    if (status == ZX_ERR_SHOULD_WAIT) {
      guard.CallUnlocked([&status, &page_request]() { status = page_request->Wait(); });
      if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
          DumpLocked(0, false);
        }
        return status;
      }
      // We dropped the lock while waiting. Check the invariants again.
      status = establish_invariants();
      if (status != ZX_OK) {
        return status;
      }
    } else if (status != ZX_OK) {
      return status;
    }
    // Advance over pages that had already been zeroed.
    start += zeroed_len;
  }

#if DEBUG_ASSERT_IMPLEMENTED
  if (is_contiguous()) {
    uint64_t page_count_after = cow_pages_locked()->DebugGetPageCountLocked();
    DEBUG_ASSERT(page_count_after == page_count_before);
  }
#endif
  return ZX_OK;
}

zx_status_t VmObjectPaged::Resize(uint64_t s) {
  canary_.Assert();

  LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

  DEBUG_ASSERT(!is_contiguous() || !is_resizable());
  // Also rejects contiguous VMOs.
  if (!is_resizable()) {
    return ZX_ERR_UNAVAILABLE;
  }

  // round up the size to the next page size boundary and make sure we don't wrap
  zx_status_t status = RoundSize(s, &s);
  if (status != ZX_OK) {
    return status;
  }

  Guard<CriticalMutex> guard{&lock_};

  status = cow_pages_locked()->ResizeLocked(s);
  if (status != ZX_OK) {
    return status;
  }
  // We were able to successfully resize. Mark as modified.
  mark_modified_locked();
  return ZX_OK;
}

// perform some sort of copy in/out on a range of the object using a passed in lambda for the copy
// routine. The copy routine has the expected type signature of: (uint64_t src_offset, uint64_t
// dest_offset, bool write, Guard<CriticalMutex> *guard) -> zx_status_t The passed in guard may have
// its CallUnlocked member used, but if it does then ZX_OK must not be the return value. A return of
// ZX_ERR_SHOULD_WAIT implies that the attempted copy should be tried again at the exact same
// offsets.
template <typename T>
zx_status_t VmObjectPaged::ReadWriteInternalLocked(uint64_t offset, size_t len, bool write,
                                                   T copyfunc, Guard<CriticalMutex>* guard) {
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

  auto mark_modified = fit::defer([this, &dest_offset, write]() {
    if (write && dest_offset > 0) {
      // We wrote something, so mark as modified.
      AssertHeld(lock_);
      mark_modified_locked();
    }
  });

  __UNINITIALIZED LookupInfo pages;
  // Record the current generation count, we can use this to attempt to avoid re-performing checks
  // whilst copying.
  uint64_t gen_count = GetHierarchyGenerationCountLocked();
  // The PageRequest is a non-trivial object so we declare it outside the loop to avoid having to
  // construct and deconstruct it each iteration. It is tolerant of being reused and will
  // reinitialize itself if needed.
  __UNINITIALIZED LazyPageRequest page_request;
  while (len > 0) {
    const size_t first_page_offset = ROUNDDOWN(src_offset, PAGE_SIZE);
    const size_t last_page_offset = ROUNDDOWN(src_offset + len - 1, PAGE_SIZE);
    const size_t max_pages = (last_page_offset - first_page_offset) / PAGE_SIZE + 1;

    // fault in the page(s)
    zx_status_t status = LookupPagesLocked(
        first_page_offset, VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0),
        VmObject::DirtyTrackingAction::DirtyAllPagesOnWrite,
        ktl::min(max_pages, LookupInfo::kMaxPages), nullptr, &page_request, &pages);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // Must block on asynchronous page requests whilst not holding the lock.
      DEBUG_ASSERT(can_block_on_page_requests());
      guard->CallUnlocked([&status, &page_request]() { status = page_request->Wait(); });
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
    DEBUG_ASSERT(pages.num_pages > 0);
    for (uint32_t i = 0; i < pages.num_pages; i++) {
      DEBUG_ASSERT(len > 0);
      const size_t page_offset = src_offset % PAGE_SIZE;
      const size_t tocopy = ktl::min(PAGE_SIZE - page_offset, len);
      paddr_t pa = pages.paddrs[i];

      // Compute the kernel mapping of this page.
      char* page_ptr = reinterpret_cast<char*>(paddr_to_physmap(pa));

      // Call the copy routine. If the copy was successful then ZX_OK is returned, otherwise
      // ZX_ERR_SHOULD_WAIT may be returned to indicate the copy failed but we can retry it. If we
      // can retry, but our generation count hasn't changed, then we know that this VMO is unchanged
      // and we don't need to re-perform checks or lookup the page again.
      do {
        status = copyfunc(page_ptr + page_offset, dest_offset, tocopy, guard);
      } while (unlikely(status == ZX_ERR_SHOULD_WAIT &&
                        gen_count == GetHierarchyGenerationCountLocked()));

      if (status == ZX_ERR_SHOULD_WAIT) {
        // The generation count changed so we must recheck properties. If all is good we cannot
        // simply retry the copy. As the underlying page could have changed, so we retry the loop
        // from the top, stashing the new generation count.
        gen_count = GetHierarchyGenerationCountLocked();
        status = check();
        if (status == ZX_OK) {
          break;
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
                            Guard<CriticalMutex>* guard) -> zx_status_t {
    memcpy(ptr + offset, src, len);
    return ZX_OK;
  };

  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }

  Guard<CriticalMutex> guard{&lock_};

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
                             Guard<CriticalMutex>* guard) -> zx_status_t {
    memcpy(dst, ptr + offset, len);
    return ZX_OK;
  };

  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }

  Guard<CriticalMutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::CacheOp(uint64_t offset, uint64_t len, CacheOpType type) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{&lock_};

  // verify that the range is within the object
  if (unlikely(!InRange(offset, len, size_locked()))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // This cannot overflow as we already checked the range.
  const uint64_t end_offset = offset + len;

  // For syncing instruction caches there may be work that is more efficient to batch together, and
  // so we use an abstract consistency manager to optimize it for the given architecture.
  ArchVmICacheConsistencyManager sync_cm;

  return cow_pages_locked()->LookupReadableLocked(
      offset, len, [&sync_cm, offset, end_offset, type](uint64_t page_offset, paddr_t pa) {
        // This cannot overflow due to the maximum possible size of a VMO.
        const uint64_t page_end = page_offset + PAGE_SIZE;

        // Determine our start and end in terms of vmo offset
        const uint64_t start = ktl::max(page_offset, offset);
        const uint64_t end = ktl::min(end_offset, page_end);

        // Translate to inter-page offset
        DEBUG_ASSERT(start >= page_offset);
        const uint64_t op_start_offset = start - page_offset;
        DEBUG_ASSERT(op_start_offset < PAGE_SIZE);

        DEBUG_ASSERT(end > start);
        const uint64_t op_len = end - start;

        CacheOpPhys(pa + op_start_offset, op_len, type, sync_cm);
        return ZX_ERR_NEXT;
      });
}

zx_status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len,
                                  VmObject::LookupFunction lookup_fn) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{&lock_};

  return cow_pages_locked()->LookupLocked(offset, len, ktl::move(lookup_fn));
}

zx_status_t VmObjectPaged::LookupContiguous(uint64_t offset, uint64_t len, paddr_t* out_paddr) {
  canary_.Assert();

  if (unlikely(len == 0 || !IS_PAGE_ALIGNED(offset))) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{&lock_};

  if (unlikely(!InRange(offset, len, size_locked()))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (unlikely(!is_contiguous() && (len != PAGE_SIZE))) {
    // Multi-page lookup only supported for contiguous VMOs.
    return ZX_ERR_BAD_STATE;
  }

  // Verify that all pages are present, and assert that the present pages are contiguous since we
  // only support len > PAGE_SIZE for contiguous VMOs.
  bool page_seen = false;
  uint64_t first_offset = 0;
  paddr_t first_paddr = 0;
  uint64_t count = 0;
  // This has to work for child slices with non-zero parent_offset_ also, which means even if all
  // pages are present, the first cur_offset can be offset + parent_offset_.
  zx_status_t status = cow_pages_locked()->LookupLocked(
      offset, len,
      [&page_seen, &first_offset, &first_paddr, &count](uint64_t cur_offset, paddr_t pa) mutable {
        ++count;
        if (!page_seen) {
          first_offset = cur_offset;
          first_paddr = pa;
          page_seen = true;
        }
        ASSERT(first_paddr + (cur_offset - first_offset) == pa);
        return ZX_ERR_NEXT;
      });
  ASSERT(status == ZX_OK);
  if (count != len / PAGE_SIZE) {
    return ZX_ERR_NOT_FOUND;
  }
  if (out_paddr) {
    *out_paddr = first_paddr;
  }
  return ZX_OK;
}

zx_status_t VmObjectPaged::ReadUser(VmAspace* current_aspace, user_out_ptr<char> ptr,
                                    uint64_t offset, size_t len, size_t* out_actual) {
  canary_.Assert();

  if (out_actual != nullptr) {
    *out_actual = 0;
  }

  // read routine that uses copy_to_user
  auto read_routine = [ptr, current_aspace, out_actual](
                          const char* src, size_t offset, size_t len,
                          Guard<CriticalMutex>* guard) -> zx_status_t {
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
    if (copy_result.status != ZX_OK) {
      return ZX_ERR_NOT_FOUND;
    }

    if (out_actual != nullptr) {
      *out_actual += len;
    }
    return ZX_OK;
  };

  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }

  Guard<CriticalMutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, false, read_routine, &guard);
}

zx_status_t VmObjectPaged::WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr,
                                     uint64_t offset, size_t len, size_t* out_actual,
                                     const OnWriteBytesTransferredCallback& on_bytes_transferred) {
  canary_.Assert();

  if (out_actual != nullptr) {
    *out_actual = 0;
  }

  // write routine that uses copy_from_user
  auto write_routine = [ptr, current_aspace, base_vmo_offset = offset, out_actual,
                        &on_bytes_transferred](char* dst, size_t offset, size_t len,
                                               Guard<CriticalMutex>* guard) -> zx_status_t {
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
    if (copy_result.status != ZX_OK) {
      return ZX_ERR_NOT_FOUND;
    }

    if (out_actual != nullptr) {
      *out_actual += len;
    }

    if (on_bytes_transferred) {
      on_bytes_transferred(base_vmo_offset + offset, len);
    }

    return ZX_OK;
  };

  if (can_block_on_page_requests()) {
    lockdep::AssertNoLocksHeld();
  }

  Guard<CriticalMutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  canary_.Assert();

  Guard<CriticalMutex> src_guard{&lock_};

  // This is only used by the userpager API, which has significant restrictions on
  // what sorts of vmos are acceptable. If splice starts being used in more places,
  // then this restriction might need to be lifted.
  //
  // TODO: Check that the region is locked once locking is implemented
  if (is_contiguous()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (children_list_len_) {
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

  __UNINITIALIZED LazyPageRequest page_request;
  while (len > 0) {
    Guard<CriticalMutex> guard{&lock_};

    // It is possible that supply pages fails and we increment the gen count needlessly, but the
    // user is certainly expecting it to succeed.
    IncrementHierarchyGenerationCountLocked();

    uint64_t supply_len = 0;
    zx_status_t status = cow_pages_locked()->SupplyPagesLocked(
        offset, len, pages, /*new_zeroed_pages=*/false, &supply_len, &page_request);
    if (status != ZX_ERR_SHOULD_WAIT && status != ZX_OK) {
      return status;
    }
    // We would only have failed to supply anything if status was not ZX_OK, which in this case
    // would be ZX_ERR_SHOULD_WAIT as that is the only non-OK status we can reach here with.
    DEBUG_ASSERT(supply_len > 0 || status == ZX_ERR_SHOULD_WAIT);
    // We shoud have supplied the entire range requested if the status was ZX_OK.
    DEBUG_ASSERT(status != ZX_OK || supply_len == len);
    // We should not have supplied any more than the requested range.
    DEBUG_ASSERT(supply_len <= len);

    // Record the completed portion.
    offset += supply_len;
    len -= supply_len;

    if (status == ZX_ERR_SHOULD_WAIT) {
      guard.CallUnlocked([&page_request, &status] { status = page_request->Wait(); });
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  return ZX_OK;
}

zx_status_t VmObjectPaged::DirtyPages(uint64_t offset, uint64_t len) {
  zx_status_t status;
  // It is possible to encounter delayed PMM allocations, which requires waiting on the
  // page_request.
  __UNINITIALIZED LazyPageRequest page_request;

  // Initialize a list of allocated pages that DirtyPagesLocked will allocate any new pages into
  // before inserting them in the VMO. Allocated pages can therefore be shared across multiple calls
  // to DirtyPagesLocked. Instead of having to allocate and free pages in case DirtyPagesLocked
  // cannot successfully dirty the entire range atomically, we can just hold on to the allocated
  // pages and use them for the next call. This ensures that we are making forward progress with
  // each successive call to DirtyPagesLocked.
  list_node alloc_list;
  list_initialize(&alloc_list);
  auto alloc_list_cleanup = fit::defer([&alloc_list]() -> void {
    if (!list_is_empty(&alloc_list)) {
      pmm_free(&alloc_list);
    }
  });

  Guard<CriticalMutex> guard{&lock_};
  do {
    status = cow_pages_locked()->DirtyPagesLocked(offset, len, &alloc_list, &page_request);
    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_status_t wait_status;
      guard.CallUnlocked([&page_request, &wait_status]() { wait_status = page_request->Wait(); });
      if (wait_status != ZX_OK) {
        return wait_status;
      }
      // If the wait was successful, loop around and try the call again, which will re-validate any
      // state that might have changed when the lock was dropped.
    }
  } while (status == ZX_ERR_SHOULD_WAIT);
  return status;
}

zx_status_t VmObjectPaged::SetMappingCachePolicy(const uint32_t cache_policy) {
  // Is it a valid cache flag?
  if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<CriticalMutex> guard{&lock_};

  // conditions for allowing the cache policy to be set:
  // 1) vmo either has no pages committed currently or is transitioning from being cached
  // 2) vmo has no pinned pages
  // 3) vmo has no mappings
  // 4) vmo has no children
  // 5) vmo is not a child
  // Counting attributed pages does a sufficient job of checking for committed pages since we also
  // require no children and no parent, so attribution == precisely our pages.
  if (cow_pages_locked()->AttributedPagesInRangeLocked(0, size_locked()) != AttributionCounts{} &&
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
  if (cow_pages_locked()->pinned_page_count_locked() > 0) {
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
    // No need to perform clean/invalidate if size is zero because there can be no pages.
    if (size_locked() > 0) {
      zx_status_t status = cow_pages_locked()->LookupLocked(
          0, size_locked(), [](uint64_t offset, paddr_t pa) mutable {
            arch_clean_invalidate_cache_range((vaddr_t)paddr_to_physmap(pa), PAGE_SIZE);
            return ZX_ERR_NEXT;
          });
      if (status != ZX_OK) {
        return status;
      }
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
    m.assert_object_lock();
    if (op == RangeChangeOp::Unmap) {
      m.AspaceUnmapVmoRangeLocked(aligned_offset, aligned_len);
    } else if (op == RangeChangeOp::RemoveWrite) {
      m.AspaceRemoveWriteVmoRangeLocked(aligned_offset, aligned_len);
    } else if (op == RangeChangeOp::DebugUnpin) {
      m.AspaceDebugUnpinLocked(aligned_offset, aligned_len);
    } else {
      panic("Unknown RangeChangeOp %d\n", static_cast<int>(op));
    }
  }
}

zx_status_t VmObjectPaged::LockRange(uint64_t offset, uint64_t len,
                                     zx_vmo_lock_state_t* lock_state_out) {
  if (!is_discardable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Guard<CriticalMutex> guard{&lock_};
  return cow_pages_locked()->LockRangeLocked(offset, len, lock_state_out);
}

zx_status_t VmObjectPaged::TryLockRange(uint64_t offset, uint64_t len) {
  if (!is_discardable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Guard<CriticalMutex> guard{&lock_};
  return cow_pages_locked()->TryLockRangeLocked(offset, len);
}

zx_status_t VmObjectPaged::UnlockRange(uint64_t offset, uint64_t len) {
  if (!is_discardable()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Guard<CriticalMutex> guard{&lock_};
  return cow_pages_locked()->UnlockRangeLocked(offset, len);
}
