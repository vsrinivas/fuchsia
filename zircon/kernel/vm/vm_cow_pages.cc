// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_cow_pages.h"

#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <trace.h>

#include <kernel/range_check.h>
#include <ktl/move.h>
#include <lk/init.h>
#include <vm/anonymous_page_requester.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/stack_owned_loaned_pages_interval.h>
#include <vm/vm_cow_pages.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_page_list.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

// add expensive code to do a full validation of the VMO at various points.
#define VMO_VALIDATION (0 || (LK_DEBUGLEVEL > 2))

// Assertion that is only enabled if VMO_VALIDATION is enabled.
#define VMO_VALIDATION_ASSERT(x) \
  do {                           \
    if (VMO_VALIDATION) {        \
      ASSERT(x);                 \
    }                            \
  } while (0)

// Add not-as-expensive code to do some extra validation at various points.  This is off in normal
// debug builds because it can add O(n) validation to an O(1) operation, so can still make things
// slower, despite not being as slow as VMO_VALIDATION.
#define VMO_FRUGAL_VALIDATION (0 || (LK_DEBUGLEVEL > 2))

// Assertion that is only enabled if VMO_FRUGAL_VALIDATION is enabled.
#define VMO_FRUGAL_VALIDATION_ASSERT(x) \
  do {                                  \
    if (VMO_FRUGAL_VALIDATION) {        \
      ASSERT(x);                        \
    }                                   \
  } while (0)

namespace {

KCOUNTER(vm_vmo_marked_latency_sensitive, "vm.vmo.latency_sensitive.marked")
KCOUNTER(vm_vmo_latency_sensitive_destroyed, "vm.vmo.latency_sensitive.destroyed")

void ZeroPage(paddr_t pa) {
  void* ptr = paddr_to_physmap(pa);
  DEBUG_ASSERT(ptr);

  arch_zero_page(ptr);
}

void ZeroPage(vm_page_t* p) {
  paddr_t pa = p->paddr();
  ZeroPage(pa);
}

bool IsZeroPage(vm_page_t* p) {
  uint64_t* base = (uint64_t*)paddr_to_physmap(p->paddr());
  for (int i = 0; i < PAGE_SIZE / (int)sizeof(uint64_t); i++) {
    if (base[i] != 0)
      return false;
  }
  return true;
}

void InitializeVmPage(vm_page_t* p) {
  DEBUG_ASSERT(p->state() == vm_page_state::ALLOC);
  p->set_state(vm_page_state::OBJECT);
  p->object.pin_count = 0;
  p->object.cow_left_split = 0;
  p->object.cow_right_split = 0;
  p->object.always_need = 0;
  p->object.dirty_state = uint8_t(VmCowPages::DirtyState::Untracked);
}

inline uint64_t CheckedAdd(uint64_t a, uint64_t b) {
  uint64_t result;
  bool overflow = add_overflow(a, b, &result);
  DEBUG_ASSERT(!overflow);
  return result;
}

// TODO(fxbug.dev/60238): Implement this once compressed pages are supported and Reference types
// can be generated.
void FreeReference(VmPageOrMarker::ReferenceValue content) {
  panic("Reference should never be generated.");
}

}  // namespace

VmCowPages::DiscardableList VmCowPages::discardable_reclaim_candidates_ = {};
VmCowPages::DiscardableList VmCowPages::discardable_non_reclaim_candidates_ = {};

fbl::DoublyLinkedList<VmCowPages::Cursor*> VmCowPages::discardable_vmos_cursors_ = {};

// Helper class for collecting pages to performed batched Removes from the page queue to not incur
// its spinlock overhead for every single page. Pages that it removes from the page queue get placed
// into a provided list. Note that pages are not moved into the list until *after* Flush has been
// called and Flush must be called prior to object destruction.
//
// This class has a large internal array and should be marked uninitialized.
class BatchPQRemove {
 public:
  BatchPQRemove(list_node_t* freed_list) : freed_list_(freed_list) {}
  ~BatchPQRemove() { DEBUG_ASSERT(count_ == 0); }
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BatchPQRemove);

  // Add a page to the batch set. Automatically calls |Flush| if the limit is reached.
  void Push(vm_page_t* page) {
    DEBUG_ASSERT(page);
    DEBUG_ASSERT(count_ < kMaxPages);
    pages_[count_] = page;
    count_++;
    if (count_ == kMaxPages) {
      Flush();
    }
  }

  // Removes any content from the supplied |page_or_marker| and either calls |Push| or otherwise
  // frees it. Always leaves the |page_or_marker| in the empty state.
  // Automatically calls |Flush| if the limit on pages is reached.
  void PushContent(VmPageOrMarker* page_or_marker) {
    if (page_or_marker->IsPage()) {
      Push(page_or_marker->ReleasePage());
    } else if (page_or_marker->IsReference()) {
      // TODO(fxbug.dev/60238): Consider whether it is worth batching these.
      FreeReference(page_or_marker->ReleaseReference());
    } else {
      *page_or_marker = VmPageOrMarker::Empty();
    }
  }

  // Performs |Remove| on any pending pages. This allows you to know that all pages are in the
  // original list so that you can do operations on the list.
  void Flush() {
    if (count_ > 0) {
      pmm_page_queues()->RemoveArrayIntoList(pages_, count_, freed_list_);
      freed_count_ += count_;
      count_ = 0;
    }
  }

  // Returns the number of pages that were added to |freed_list_| by calls to Flush(). The
  // |freed_count_| counter keeps a running count of freed pages as they are removed and added to
  // |freed_list_|, avoiding having to walk |freed_list_| to compute its length.
  size_t freed_count() const { return freed_count_; }

  // Produces a callback suitable for passing to VmPageList::RemovePages that will |PushContent| all
  // items.
  auto RemovePagesCallback() {
    return [this](VmPageOrMarker* p, uint64_t off) {
      PushContent(p);
      return ZX_ERR_NEXT;
    };
  }

 private:
  // The value of 64 was chosen as there is minimal performance gains originally measured by using
  // higher values. There is an incentive on this being as small as possible due to this typically
  // being created on the stack, and our stack space is limited.
  static constexpr size_t kMaxPages = 64;

  size_t count_ = 0;
  size_t freed_count_ = 0;
  vm_page_t* pages_[kMaxPages];
  list_node_t* freed_list_ = nullptr;
};

// Allocates a new page and populates it with the data at |parent_paddr|.
zx_status_t VmCowPages::AllocateCopyPage(uint32_t pmm_alloc_flags, paddr_t parent_paddr,
                                         list_node_t* alloc_list, LazyPageRequest* request,
                                         vm_page_t** clone) {
  DEBUG_ASSERT(request || !(pmm_alloc_flags & PMM_ALLOC_FLAG_CAN_WAIT));

  vm_page_t* p_clone = nullptr;
  if (alloc_list) {
    p_clone = list_remove_head_type(alloc_list, vm_page, queue_node);
  }

  paddr_t pa_clone;
  if (p_clone) {
    pa_clone = p_clone->paddr();
  } else {
    zx_status_t status = CacheAllocPage(pmm_alloc_flags, &p_clone, &pa_clone);
    if (status != ZX_OK) {
      DEBUG_ASSERT(!p_clone);
      if (status == ZX_ERR_SHOULD_WAIT) {
        status = AnonymousPageRequester::Get().FillRequest(request->get());
      }
      return status;
    }
    DEBUG_ASSERT(p_clone);
  }

  InitializeVmPage(p_clone);

  void* dst = paddr_to_physmap(pa_clone);
  DEBUG_ASSERT(dst);

  if (parent_paddr != vm_get_zero_page_paddr()) {
    // do a direct copy of the two pages
    const void* src = paddr_to_physmap(parent_paddr);
    DEBUG_ASSERT(src);
    memcpy(dst, src, PAGE_SIZE);
  } else {
    // avoid pointless fetches by directly zeroing dst
    arch_zero_page(dst);
  }

  *clone = p_clone;

  return ZX_OK;
}

zx_status_t VmCowPages::CacheAllocPage(uint alloc_flags, vm_page_t** p, paddr_t* pa) {
  if (!page_cache_) {
    return pmm_alloc_page(alloc_flags, p, pa);
  }

  zx::result result = page_cache_.Allocate(1, alloc_flags);
  if (result.is_error()) {
    return result.error_value();
  }

  vm_page_t* page = list_remove_head_type(&result->page_list, vm_page_t, queue_node);
  DEBUG_ASSERT(page != nullptr);
  DEBUG_ASSERT(result->page_list.is_empty());

  *p = page;
  *pa = page->paddr();
  return ZX_OK;
}

void VmCowPages::CacheFree(list_node_t* list) {
  if (!page_cache_) {
    pmm_free(list);
  }

  page_cache_.Free(ktl::move(*list));
}

void VmCowPages::CacheFree(vm_page_t* p) {
  if (!page_cache_) {
    pmm_free_page(p);
  }

  page_cache::PageCache::PageList list;
  list_add_tail(&list, &p->queue_node);

  page_cache_.Free(ktl::move(list));
}

// TODO(fxbug.dev/60238): Implement this once compressed pages are supported and Reference types
// can be generated.
zx_status_t VmCowPages::MakePageFromReference(VmPageOrMarkerRef page_or_mark,
                                              LazyPageRequest* page_request) {
  DEBUG_ASSERT(page_or_mark->IsReference());
  panic("Reference should never be generated.");
  return ZX_OK;
}

zx_status_t VmCowPages::ReplaceReferenceWithPageLocked(VmPageOrMarkerRef page_or_mark,
                                                       uint64_t offset,
                                                       LazyPageRequest* page_request) {
  // First replace the ref with a page.
  zx_status_t status = MakePageFromReference(page_or_mark, page_request);
  if (status != ZX_OK) {
    return status;
  }
  IncrementHierarchyGenerationCountLocked();
  // Add the new page to the page queues for tracking. References are by definition not pinned, so
  // we know this is not wired.
  SetNotPinnedLocked(page_or_mark->Page(), offset);
  return ZX_OK;
}

VmCowPages::VmCowPages(ktl::unique_ptr<VmCowPagesContainer> cow_container,
                       const fbl::RefPtr<VmHierarchyState> hierarchy_state_ptr,
                       VmCowPagesOptions options, uint32_t pmm_alloc_flags, uint64_t size,
                       fbl::RefPtr<PageSource> page_source)
    : VmHierarchyBase(ktl::move(hierarchy_state_ptr)),
      container_(fbl::AdoptRef(cow_container.release())),
      debug_retained_raw_container_(container_.get()),
      options_(options),
      size_(size),
      pmm_alloc_flags_(pmm_alloc_flags),
      page_source_(ktl::move(page_source)) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(!(pmm_alloc_flags & PMM_ALLOC_FLAG_CAN_BORROW));
}

void VmCowPages::fbl_recycle() {
  canary_.Assert();

  // To prevent races with a hidden parent creation or merging, it is necessary to hold the lock
  // over the is_hidden and parent_ check and into the subsequent removal call.
  // It is safe to grab the lock here because we are careful to never cause the last reference to
  // a VmCowPages to be dropped in this code whilst holding the lock. The single place we drop a
  // a VmCowPages reference that could trigger a deletion is in this destructor when parent_ is
  // dropped, but that is always done without holding the lock.
  {  // scope guard
    Guard<CriticalMutex> guard{&lock_};
    VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
    // If we're not a hidden vmo, then we need to remove ourself from our parent. This needs
    // to be done before emptying the page list so that a hidden parent can't merge into this
    // vmo and repopulate the page list.
    if (!is_hidden_locked()) {
      if (parent_) {
        AssertHeld(parent_->lock_);
        parent_->RemoveChildLocked(this);
        // Avoid recursing destructors when we delete our parent by using the deferred deletion
        // method. See common in parent else branch for why we can avoid this on a hidden parent.
        if (!parent_->is_hidden_locked()) {
          guard.CallUnlocked([this, parent = ktl::move(parent_)]() mutable {
            hierarchy_state_ptr_->DoDeferredDelete(ktl::move(parent));
          });
        }
      }
    } else {
      // Most of the hidden vmo's state should have already been cleaned up when it merged
      // itself into its child in ::RemoveChildLocked.
      DEBUG_ASSERT(children_list_len_ == 0);
      DEBUG_ASSERT(page_list_.HasNoPageOrRef());
      // Even though we are hidden we might have a parent. Unlike in the other branch of this if we
      // do not need to perform any deferred deletion. The reason for this is that the deferred
      // deletion mechanism is intended to resolve the scenario where there is a chain of 'one ref'
      // parent pointers that will chain delete. However, with hidden parents we *know* that a
      // hidden parent has two children (and hence at least one other ref to it) and so we cannot be
      // in a one ref chain. Even if N threads all tried to remove children from the hierarchy at
      // once, this would ultimately get serialized through the lock and the hierarchy would go from
      //
      //          [..]
      //           /
      //          A                             [..]
      //         / \                             /
      //        B   E           TO         B    A
      //       / \                        /    / \.
      //      C   D                      C    D   E
      //
      // And so each serialized deletion breaks of a discrete two VMO chain that can be safely
      // finalized with one recursive step.
    }

    RemoveFromDiscardableListLocked();

    // We stack-own loaned pages between removing the page from PageQueues and freeing the page via
    // call to FreePagesLocked().
    __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

    // Cleanup page lists and page sources.
    list_node_t list;
    list_initialize(&list);

    __UNINITIALIZED BatchPQRemove page_remover(&list);
    // free all of the pages attached to us
    page_list_.RemoveAllContent([&page_remover](VmPageOrMarker&& p) {
      ASSERT(!p.IsPage() || p.Page()->object.pin_count == 0);
      page_remover.PushContent(&p);
    });
    page_remover.Flush();

    FreePagesLocked(&list, /*freeing_owned_pages=*/true);

    // We must Close() after removing pages, so that all pages will be loaned by the time
    // PhysicalPageProvider::OnClose() calls pmm_delete_lender() on the whole physical range.
    if (page_source_) {
      page_source_->Close();
    }

    // Update counters
    if (is_latency_sensitive_) {
      vm_vmo_latency_sensitive_destroyed.Add(1);
    }
  }  // ~guard

  // Release the ref that VmCowPages keeps on VmCowPagesContainer.
  container_.reset();
}

VmCowPages::~VmCowPages() {
  // All the explicit cleanup happens in fbl_recycle().  Only asserts and implicit cleanup happens
  // in the destructor.
  canary_.Assert();
  // While use a ktl::optional<VmCowPages> in VmCowPagesContainer, we don't intend to reset() it
  // early.
  DEBUG_ASSERT(0 == ref_count_debug());
  // We only intent to delete VmCowPages when the container is also deleting, and the container
  // won't be deleting unless it's ref is 0.
  DEBUG_ASSERT(!container_);
  DEBUG_ASSERT(0 == debug_retained_raw_container_->ref_count_debug());
}

bool VmCowPages::DedupZeroPage(vm_page_t* page, uint64_t offset) {
  canary_.Assert();

  Guard<CriticalMutex> guard{&lock_};

  // TODO(fxb/101641): Formalize this.
  // Forbid zero page deduping if this is latency sensitive.
  if (is_latency_sensitive_) {
    return false;
  }

  if (paged_ref_) {
    AssertHeld(paged_ref_->lock_ref());
    if (!paged_ref_->CanDedupZeroPagesLocked()) {
      return false;
    }
  }

  // Check this page is still a part of this VMO. object.page_offset could be wrong, but there's no
  // harm in looking up a random slot as we'll then notice it's the wrong page.
  // Also ignore any references since we cannot efficiently scan them, and they should presumably
  // already be deduped.
  // Pinned pages cannot be decommited and so also must not be committed. We must also not decommit
  // pages from kernel VMOs, as the kernel cannot fault them back in, but all kernel pages will be
  // pinned.
  VmPageOrMarkerRef page_or_marker = page_list_.LookupMutable(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page ||
      page->object.pin_count > 0 || (is_page_dirty_tracked(page) && !is_page_clean(page))) {
    return false;
  }

  // We expect most pages to not be zero, as such we will first do a 'racy' zero page check where
  // we leave write permissions on the page. If the page isn't zero, which is our hope, then we
  // haven't paid the price of modifying page tables.
  if (!IsZeroPage(page_or_marker->Page())) {
    return false;
  }

  RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::RemoveWrite);

  if (IsZeroPage(page_or_marker->Page())) {
    // We stack-own loaned pages from when they're removed until they're freed.
    __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

    // Replace the slot with a marker.
    VmPageOrMarker new_marker = VmPageOrMarker::Marker();
    VmPageOrMarker old_page;
    zx_status_t status =
        AddPageLocked(&new_marker, offset, CanOverwriteContent::NonZero, &old_page);
    DEBUG_ASSERT(status == ZX_OK);
    DEBUG_ASSERT(old_page.IsPage());

    // Free the old page.
    vm_page_t* released_page = old_page.ReleasePage();
    pmm_page_queues()->Remove(released_page);
    DEBUG_ASSERT(!list_in_list(&released_page->queue_node));
    FreePageLocked(released_page, /*freeing_owned_page=*/true);

    eviction_event_count_++;
    IncrementHierarchyGenerationCountLocked();
    VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
    VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
    return true;
  }
  return false;
}

zx_status_t VmCowPages::Create(fbl::RefPtr<VmHierarchyState> root_lock, VmCowPagesOptions options,
                               uint32_t pmm_alloc_flags, uint64_t size,
                               fbl::RefPtr<VmCowPages>* cow_pages) {
  DEBUG_ASSERT(!(options & VmCowPagesOptions::kInternalOnlyMask));
  fbl::AllocChecker ac;
  auto cow = NewVmCowPages(&ac, ktl::move(root_lock), options, pmm_alloc_flags, size, nullptr);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  *cow_pages = ktl::move(cow);
  return ZX_OK;
}

zx_status_t VmCowPages::CreateExternal(fbl::RefPtr<PageSource> src, VmCowPagesOptions options,
                                       fbl::RefPtr<VmHierarchyState> root_lock, uint64_t size,
                                       fbl::RefPtr<VmCowPages>* cow_pages) {
  DEBUG_ASSERT(!(options & VmCowPagesOptions::kInternalOnlyMask));
  fbl::AllocChecker ac;
  auto cow =
      NewVmCowPages(&ac, ktl::move(root_lock), options, PMM_ALLOC_FLAG_ANY, size, ktl::move(src));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  {
    // If the page source preserves content, initialize supply_zero_offset_ to size. All initial
    // content for a newly created VMO is provided by the page source, i.e. there is no content that
    // the kernel implicitly supplies with zero.
    Guard<CriticalMutex> guard{&cow->lock_};
    if (cow->is_source_preserving_page_content()) {
      DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
      cow->UpdateSupplyZeroOffsetLocked(size);
    }
  }

  *cow_pages = ktl::move(cow);
  return ZX_OK;
}

void VmCowPages::ReplaceChildLocked(VmCowPages* old, VmCowPages* new_child) {
  canary_.Assert();
  children_list_.replace(*old, new_child);
}

void VmCowPages::DropChildLocked(VmCowPages* child) {
  canary_.Assert();
  DEBUG_ASSERT(children_list_len_ > 0);
  children_list_.erase(*child);
  --children_list_len_;
}

void VmCowPages::AddChildLocked(VmCowPages* child, uint64_t offset, uint64_t root_parent_offset,
                                uint64_t parent_limit) {
  canary_.Assert();

  // As we do not want to have to return failure from this function we require root_parent_offset to
  // be calculated and validated that it does not overflow externally, but we can still assert that
  // it has been calculated correctly to prevent accidents.
  AssertHeld(child->lock_ref());
  DEBUG_ASSERT(CheckedAdd(root_parent_offset_, offset) == root_parent_offset);

  // The child should definitely stop seeing into the parent at the limit of its size.
  DEBUG_ASSERT(parent_limit <= child->size_);

  // Write in the parent view values.
  child->root_parent_offset_ = root_parent_offset;
  child->parent_offset_ = offset;
  child->parent_limit_ = parent_limit;

  // This child should be in an initial state and these members should be clear.
  DEBUG_ASSERT(!child->partial_cow_release_);
  DEBUG_ASSERT(child->parent_start_limit_ == 0);

  child->page_list_.InitializeSkew(page_list_.GetSkew(), offset);

  child->parent_ = fbl::RefPtr(this);
  children_list_.push_front(child);
  children_list_len_++;
}

zx_status_t VmCowPages::CreateChildSliceLocked(uint64_t offset, uint64_t size,
                                               fbl::RefPtr<VmCowPages>* cow_slice) {
  LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(CheckedAdd(offset, size) <= size_);

  // If this is a slice re-home this on our parent. Due to this logic we can guarantee that any
  // slice parent is, itself, not a slice.
  // We are able to do this for two reasons:
  //  * Slices are subsets and so every position in a slice always maps back to the paged parent.
  //  * Slices are not permitted to be resized and so nothing can be done on the intermediate parent
  //    that requires us to ever look at it again.
  if (is_slice_locked()) {
    DEBUG_ASSERT(parent_);
    AssertHeld(parent_->lock_ref());
    DEBUG_ASSERT(!parent_->is_slice_locked());
    return parent_->CreateChildSliceLocked(offset + parent_offset_, size, cow_slice);
  }

  fbl::AllocChecker ac;
  // Slices just need the slice option and default alloc flags since they will propagate any
  // operation up to a parent and use their options and alloc flags.
  auto slice = NewVmCowPages(&ac, hierarchy_state_ptr_, VmCowPagesOptions::kSlice,
                             PMM_ALLOC_FLAG_ANY, size, nullptr);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  // At this point slice must *not* be destructed in this function, as doing so would cause a
  // deadlock. That means from this point on we *must* succeed and any future error checking needs
  // to be added prior to creation.

  AssertHeld(slice->lock_);

  // As our slice must be in range of the parent it is impossible to have the accumulated parent
  // offset overflow.
  uint64_t root_parent_offset = CheckedAdd(offset, root_parent_offset_);
  CheckedAdd(root_parent_offset, size);

  AddChildLocked(slice.get(), offset, root_parent_offset, size);

  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(slice->DebugValidateVmoPageBorrowingLocked());

  *cow_slice = slice;
  return ZX_OK;
}

void VmCowPages::CloneParentIntoChildLocked(fbl::RefPtr<VmCowPages>& child) {
  AssertHeld(child->lock_ref());
  // This function is invalid to call if any pages are pinned as the unpin after we change the
  // backlink will not work.
  DEBUG_ASSERT(pinned_page_count_ == 0);
  // We are going to change our linked VmObjectPaged to eventually point to our left child instead
  // of us, so we need to make the left child look equivalent. To do this it inherits our
  // children, attribution id and eviction count and is sized to completely cover us.
  for (auto& c : children_list_) {
    AssertHeld(c.lock_ref());
    c.parent_ = child;
  }
  child->children_list_ = ktl::move(children_list_);
  child->children_list_len_ = children_list_len_;
  children_list_len_ = 0;
  child->eviction_event_count_ = eviction_event_count_;
  child->page_attribution_user_id_ = page_attribution_user_id_;
  AddChildLocked(child.get(), 0, root_parent_offset_, size_);

  // Time to change the VmCowPages that our paged_ref_ is point to.
  if (paged_ref_) {
    child->paged_ref_ = paged_ref_;
    AssertHeld(paged_ref_->lock_ref());
    fbl::RefPtr<VmCowPages> __UNUSED previous =
        paged_ref_->SetCowPagesReferenceLocked(ktl::move(child));
    // Validate that we replaced a reference to ourself as we expected, this ensures we can safely
    // drop the refptr without triggering our own destructor, since we know someone else must be
    // holding a refptr to us to be in this function.
    DEBUG_ASSERT(previous.get() == this);
    paged_ref_ = nullptr;
  }
}

zx_status_t VmCowPages::CreateCloneLocked(CloneType type, uint64_t offset, uint64_t size,
                                          fbl::RefPtr<VmCowPages>* cow_child) {
  LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(!is_hidden_locked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  // All validation *must* be performed here prior to construction the VmCowPages, as the
  // destructor for VmCowPages may acquire the lock, which we are already holding.

  switch (type) {
    case CloneType::Snapshot: {
      if (!is_cow_clonable_locked()) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      // If this is non-zero, that means that there are pages which hardware can
      // touch, so the vmo can't be safely cloned.
      // TODO: consider immediately forking these pages.
      if (pinned_page_count_locked()) {
        return ZX_ERR_BAD_STATE;
      }
      break;
    }
    case CloneType::PrivatePagerCopy:
      if (!is_private_pager_copy_supported()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      break;
  }

  uint64_t new_root_parent_offset;
  bool overflow;
  overflow = add_overflow(offset, root_parent_offset_, &new_root_parent_offset);
  if (overflow) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint64_t temp;
  overflow = add_overflow(new_root_parent_offset, size, &temp);
  if (overflow) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t child_parent_limit = offset >= size_ ? 0 : ktl::min(size, size_ - offset);

  // Invalidate everything the clone will be able to see. They're COW pages now,
  // so any existing mappings can no longer directly write to the pages.
  RangeChangeUpdateLocked(offset, size, RangeChangeOp::RemoveWrite);

  if (type == CloneType::Snapshot) {
    // We need two new VmCowPages for our two children. To avoid destructor of the first being
    // invoked if the second fails we separately perform allocations and construction.  It's fine
    // for the destructor of VmCowPagesContainer to run since the optional VmCowPages isn't emplaced
    // yet so the VmCowPages destructor doesn't run if the second fails allocation.
    fbl::AllocChecker ac;
    ktl::unique_ptr<VmCowPagesContainer> left_child_placeholder =
        ktl::make_unique<VmCowPagesContainer>(&ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    ktl::unique_ptr<VmCowPagesContainer> right_child_placeholder =
        ktl::make_unique<VmCowPagesContainer>(&ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    // At this point cow_pages must *not* be destructed in this function, as doing so would cause a
    // deadlock. That means from this point on we *must* succeed and any future error checking needs
    // to be added prior to creation.

    fbl::RefPtr<VmCowPages> left_child =
        NewVmCowPages(ktl::move(left_child_placeholder), hierarchy_state_ptr_,
                      VmCowPagesOptions::kNone, pmm_alloc_flags_, size_, nullptr);
    fbl::RefPtr<VmCowPages> right_child =
        NewVmCowPages(ktl::move(right_child_placeholder), hierarchy_state_ptr_,
                      VmCowPagesOptions::kNone, pmm_alloc_flags_, size, nullptr);

    AssertHeld(left_child->lock_ref());
    AssertHeld(right_child->lock_ref());

    // The left child becomes a full clone of us, inheriting our children, paged backref etc.
    CloneParentIntoChildLocked(left_child);

    // The right child is the, potential, subset view into the parent so has a variable offset. If
    // this view would extend beyond us then we need to clip the parent_limit to our size_, which
    // will ensure any pages in that range just get initialized from zeroes.
    AddChildLocked(right_child.get(), offset, new_root_parent_offset, child_parent_limit);

    // Transition into being the hidden node.
    options_ |= VmCowPagesOptions::kHidden;
    DEBUG_ASSERT(children_list_len_ == 2);

    *cow_child = ktl::move(right_child);

    VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
    VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
    return ZX_OK;
  } else {
    fbl::AllocChecker ac;
    auto cow_pages = NewVmCowPages(&ac, hierarchy_state_ptr_, VmCowPagesOptions::kNone,
                                   pmm_alloc_flags_, size, nullptr);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    // Walk up the parent chain until we find a good place to hang this new cow clone. A good place
    // here means the first place that has committed pages that we actually need to snapshot. In
    // doing so we need to ensure that the limits of the child we create do not end up seeing more
    // of the final parent than it would have been able to see from here.
    VmCowPages* cur = this;
    AssertHeld(cur->lock_ref());
    while (cur->parent_) {
      // There's a parent, check if there are any pages in the current range. Unless we've moved
      // outside the range of our parent, in which case we can just walk up.
      if (child_parent_limit > 0 &&
          cur->page_list_.AnyPagesInRange(offset, offset + child_parent_limit)) {
        break;
      }
      // To move to the parent we need to translate our window into |cur|.
      if (offset >= cur->parent_limit_) {
        child_parent_limit = 0;
      } else {
        child_parent_limit = ktl::min(child_parent_limit, cur->parent_limit_ - offset);
      }
      offset += cur->parent_offset_;
      cur = cur->parent_.get();
    }
    new_root_parent_offset = CheckedAdd(offset, cur->root_parent_offset_);
    cur->AddChildLocked(cow_pages.get(), offset, new_root_parent_offset, child_parent_limit);

    *cow_child = ktl::move(cow_pages);
  }

  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  AssertHeld((*cow_child)->lock_ref());
  VMO_FRUGAL_VALIDATION_ASSERT((*cow_child)->DebugValidateVmoPageBorrowingLocked());

  return ZX_OK;
}

void VmCowPages::RemoveChildLocked(VmCowPages* removed) {
  canary_.Assert();

  AssertHeld(removed->lock_);

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  if (!is_hidden_locked()) {
    DropChildLocked(removed);
    return;
  }

  // Hidden vmos always have 0 or 2 children, but we can't be here with 0 children.
  DEBUG_ASSERT(children_list_len_ == 2);
  bool removed_left = &left_child_locked() == removed;

  DropChildLocked(removed);

  VmCowPages* child = &children_list_.front();
  DEBUG_ASSERT(child);

  MergeContentWithChildLocked(removed, removed_left);

  // The child which removed itself and led to the invocation should have a reference
  // to us, in addition to child.parent_ which we are about to clear.
  DEBUG_ASSERT(ref_count_debug() >= 2);

  AssertHeld(child->lock_);
  if (child->page_attribution_user_id_ != page_attribution_user_id_) {
    // If the attribution user id of this vmo doesn't match that of its remaining child,
    // then the vmo with the matching attribution user id was just closed. In that case, we
    // need to reattribute the pages of any ancestor hidden vmos to vmos that still exist.
    //
    // The syscall API doesn't specify how pages are to be attributed among a group of COW
    // clones. One option is to pick a remaining vmo 'arbitrarily' and attribute everything to
    // that vmo. However, it seems fairer to reattribute each remaining hidden vmo with
    // its child whose user id doesn't match the vmo that was just closed. So walk up the
    // clone chain and attribute each hidden vmo to the vmo we didn't just walk through.
    auto cur = this;
    AssertHeld(cur->lock_);
    uint64_t user_id_to_skip = page_attribution_user_id_;
    while (cur->parent_ != nullptr) {
      auto parent = cur->parent_.get();
      AssertHeld(parent->lock_);
      DEBUG_ASSERT(parent->is_hidden_locked());

      if (parent->page_attribution_user_id_ == page_attribution_user_id_) {
        uint64_t new_user_id = parent->left_child_locked().page_attribution_user_id_;
        if (new_user_id == user_id_to_skip) {
          new_user_id = parent->right_child_locked().page_attribution_user_id_;
        }
        // Although user IDs can be unset for VMOs that do not have a dispatcher, copy-on-write
        // VMOs always have user level dispatchers, and should have a valid user-id set, hence we
        // should never end up re-attributing a hidden parent with an unset id.
        DEBUG_ASSERT(new_user_id != 0);
        // The 'if' above should mean that the new_user_id isn't the ID we are trying to remove
        // and isn't one we just used. For this to fail we either need a corrupt VMO hierarchy, or
        // to have labeled two leaf nodes with the same user_id, which would also be incorrect as
        // leaf nodes have unique dispatchers and hence unique ids.
        DEBUG_ASSERT(new_user_id != page_attribution_user_id_ && new_user_id != user_id_to_skip);
        parent->page_attribution_user_id_ = new_user_id;
        user_id_to_skip = new_user_id;

        cur = parent;
      } else {
        break;
      }
    }
  }

  // Drop the child from our list, but don't recurse back into this function. Then
  // remove ourselves from the clone tree.
  DropChildLocked(child);
  if (parent_) {
    AssertHeld(parent_->lock_ref());
    parent_->ReplaceChildLocked(this, child);
  }
  child->parent_ = ktl::move(parent_);

  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
}

void VmCowPages::MergeContentWithChildLocked(VmCowPages* removed, bool removed_left) {
  DEBUG_ASSERT(children_list_len_ == 1);
  VmCowPages& child = children_list_.front();
  AssertHeld(child.lock_);
  AssertHeld(removed->lock_);
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  list_node freed_pages;
  list_initialize(&freed_pages);
  __UNINITIALIZED BatchPQRemove page_remover(&freed_pages);

  const uint64_t visibility_start_offset = child.parent_offset_ + child.parent_start_limit_;
  const uint64_t merge_start_offset = child.parent_offset_;
  const uint64_t merge_end_offset = child.parent_offset_ + child.parent_limit_;

  // There's no technical reason why this merging code cannot be run if there is a page source,
  // however a bi-directional clone will never have a page source and so in case there are any
  // consequence that have no been considered, ensure we are not in this case.
  DEBUG_ASSERT(!is_source_preserving_page_content());

  page_list_.RemovePages(page_remover.RemovePagesCallback(), 0, visibility_start_offset);
  page_list_.RemovePages(page_remover.RemovePagesCallback(), merge_end_offset,
                         VmPageList::MAX_SIZE);

  if (child.parent_offset_ + child.parent_limit_ > parent_limit_) {
    // Update the child's parent limit to ensure that it won't be able to see more
    // of its new parent than this hidden vmo was able to see.
    if (parent_limit_ < child.parent_offset_) {
      child.parent_limit_ = 0;
      child.parent_start_limit_ = 0;
    } else {
      child.parent_limit_ = parent_limit_ - child.parent_offset_;
      child.parent_start_limit_ = ktl::min(child.parent_start_limit_, child.parent_limit_);
    }
  } else {
    // The child will be able to see less of its new parent than this hidden vmo was
    // able to see, so release any parent pages in that range.
    ReleaseCowParentPagesLocked(merge_end_offset, parent_limit_, &page_remover);
  }

  if (removed->parent_offset_ + removed->parent_start_limit_ < visibility_start_offset) {
    // If the removed former child has a smaller offset, then there are retained
    // ancestor pages that will no longer be visible and thus should be freed.
    ReleaseCowParentPagesLocked(removed->parent_offset_ + removed->parent_start_limit_,
                                visibility_start_offset, &page_remover);
  }

  // Adjust the child's offset so it will still see the correct range.
  bool overflow = add_overflow(parent_offset_, child.parent_offset_, &child.parent_offset_);
  // Overflow here means that something went wrong when setting up parent limits.
  DEBUG_ASSERT(!overflow);

  if (child.is_hidden_locked()) {
    // After the merge, either |child| can't see anything in parent (in which case
    // the parent limits could be anything), or |child|'s first visible offset will be
    // at least as large as |this|'s first visible offset.
    DEBUG_ASSERT(child.parent_start_limit_ == child.parent_limit_ ||
                 parent_offset_ + parent_start_limit_ <=
                     child.parent_offset_ + child.parent_start_limit_);
  } else {
    // non-hidden vmos should always have zero parent_start_limit_
    DEBUG_ASSERT(child.parent_start_limit_ == 0);
  }

  // At this point, we need to merge |this|'s page list and |child|'s page list.
  //
  // In general, COW clones are expected to share most of their pages (i.e. to fork a relatively
  // small number of pages). Because of this, it is preferable to do work proportional to the
  // number of pages which were forked into |removed|. However, there are a few things that can
  // prevent this:
  //   - If |child|'s offset is non-zero then the offsets of all of |this|'s pages will
  //     need to be updated when they are merged into |child|.
  //   - If there has been a call to ReleaseCowParentPagesLocked which was not able to
  //     update the parent limits, then there can exist pages in this vmo's page list
  //     which are not visible to |child| but can't be easily freed based on its parent
  //     limits. Finding these pages requires examining the split bits of all pages.
  //   - If |child| is hidden, then there can exist pages in this vmo which were split into
  //     |child|'s subtree and then migrated out of |child|. Those pages need to be freed, and
  //     the simplest way to find those pages is to examine the split bits.
  bool fast_merge = merge_start_offset == 0 && !partial_cow_release_ && !child.is_hidden_locked();

  if (fast_merge) {
    // Only leaf vmos can be directly removed, so this must always be true. This guarantees
    // that there are no pages that were split into |removed| that have since been migrated
    // to its children.
    DEBUG_ASSERT(!removed->is_hidden_locked());

    // Before merging, find any pages that are present in both |removed| and |this|. Those
    // pages are visibile to |child| but haven't been written to through |child|, so
    // their split bits need to be cleared. Note that ::ReleaseCowParentPagesLocked ensures
    // that pages outside of the parent limit range won't have their split bits set.
    removed->page_list_.ForEveryPageInRange(
        [removed_offset = removed->parent_offset_, this](auto* page, uint64_t offset) {
          AssertHeld(lock_);
          // Whether this is a true page, or a marker, we must check |this| for a page as either
          // represents a potential fork, even if we subsequently changed it to a marker.
          VmPageOrMarkerRef page_or_mark = page_list_.LookupMutable(offset + removed_offset);
          if (page_or_mark && page_or_mark->IsPageOrRef()) {
            // The page was definitely forked into |removed|, but
            // shouldn't be forked twice.
            DEBUG_ASSERT(page_or_mark->PageOrRefLeftSplit() ^ page_or_mark->PageOrRefRightSplit());
            page_or_mark.SetPageOrRefLeftSplit(false);
            page_or_mark.SetPageOrRefRightSplit(false);
          }
          return ZX_ERR_NEXT;
        },
        removed->parent_start_limit_, removed->parent_limit_);

    // These will be freed, but accumulate them separately for use in asserts before adding these to
    // freed_pages.
    list_node covered_pages;
    list_initialize(&covered_pages);
    __UNINITIALIZED BatchPQRemove covered_remover(&covered_pages);

    // Although not all pages in page_list_ will end up existing in child, we don't know which ones
    // will get replaced, so we must update all of the backlinks.
    {
      PageQueues* pq = pmm_page_queues();
      Guard<CriticalMutex> guard{pq->get_lock()};
      page_list_.ForEveryPage([pq, &child](auto* p, uint64_t off) {
        // Only actual content pages have backlinks, References do not and so do not need to be
        // updated.
        if (p->IsPage()) {
          AssertHeld<Lock<CriticalMutex>>(*pq->get_lock());
          vm_page_t* page = p->Page();
          pq->ChangeObjectOffsetLocked(page, &child, off);
        }
        return ZX_ERR_NEXT;
      });
    }

    // Now merge |child|'s pages into |this|, overwriting any pages present in |this|, and
    // then move that list to |child|.
    // We are going to perform a delayed free on pages removed here by concatenating |covered_pages|
    // to |freed_pages|. As a result |freed_pages| will end up with mixed ownership of pages, so
    // FreePagesLocked() will simply free the pages to the PMM. Make sure that the |child| did not
    // have a source that was handling frees, which would require more work that simply freeing
    // pages to the PMM.
    DEBUG_ASSERT(!child.is_source_handling_free_locked());
    child.page_list_.MergeOnto(
        page_list_, [&covered_remover](VmPageOrMarker&& p) { covered_remover.PushContent(&p); });
    child.page_list_ = ktl::move(page_list_);

    vm_page_t* p;
    covered_remover.Flush();
    list_for_every_entry (&covered_pages, p, vm_page_t, queue_node) {
      // The page was already present in |child|, so it should be split at least
      // once. And being split twice is obviously bad.
      ASSERT(p->object.cow_left_split ^ p->object.cow_right_split);
      ASSERT(p->object.pin_count == 0);
    }
    list_splice_after(&covered_pages, &freed_pages);
  } else {
    // Merge our page list into the child page list and update all the necessary metadata.
    struct {
      PageQueues* pq;
      bool removed_left;
      uint64_t merge_start_offset;
      VmCowPages* child;
      BatchPQRemove* page_remover;
    } state = {pmm_page_queues(), removed_left, merge_start_offset, &child, &page_remover};
    child.page_list_.MergeFrom(
        page_list_, merge_start_offset, merge_end_offset,
        [&page_remover](VmPageOrMarker&& p, uint64_t offset) { page_remover.PushContent(&p); },
        [&state](VmPageOrMarker* page_or_marker, uint64_t offset) {
          DEBUG_ASSERT(page_or_marker->IsPageOrRef());
          DEBUG_ASSERT(page_or_marker->IsReference() ||
                       page_or_marker->Page()->object.pin_count == 0);

          if (state.removed_left ? page_or_marker->PageOrRefRightSplit()
                                 : page_or_marker->PageOrRefLeftSplit()) {
            // This happens when the pages was already migrated into child but then
            // was migrated further into child's descendants. The page can be freed.
            state.page_remover->PushContent(page_or_marker);
          } else {
            // Since we recursively fork on write, if the child doesn't have the
            // page, then neither of its children do.
            page_or_marker->SetPageOrRefLeftSplit(false);
            page_or_marker->SetPageOrRefRightSplit(false);
            if (page_or_marker->IsPage()) {
              state.pq->ChangeObjectOffset(page_or_marker->Page(), state.child,
                                           offset - state.merge_start_offset);
            }
          }
        });
  }

  page_remover.Flush();
  if (!list_is_empty(&freed_pages)) {
    // |freed_pages| might also contain pages removed from a child or an ancestor, so we do not own
    // all the pages. Make sure we did not have a page source that was handling frees which would
    // require additional work on the owned pages on top of a simple free to the PMM.
    DEBUG_ASSERT(!is_source_handling_free_locked());
    FreePagesLocked(&freed_pages, /*freeing_owned_pages=*/false);
  }
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
}

void VmCowPages::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();

  size_t page_count = 0;
  size_t compressed_count = 0;
  page_list_.ForEveryPage([&page_count, &compressed_count](const auto* p, uint64_t) {
    if (p->IsPage()) {
      page_count++;
    } else if (p->IsReference()) {
      compressed_count++;
    }
    return ZX_ERR_NEXT;
  });

  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("cow_pages %p size %#" PRIx64 " offset %#" PRIx64 " start limit %#" PRIx64
         " limit %#" PRIx64 " content pages %zu compressed pages %zu ref %d parent %p\n",
         this, size_, parent_offset_, parent_start_limit_, parent_limit_, page_count,
         compressed_count, ref_count_debug(), parent_.get());

  if (page_source_) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    printf("page_source preserves content %d supply_zero_offset %#" PRIx64 "\n",
           is_source_preserving_page_content(), supply_zero_offset_);
    page_source_->Dump(depth + 1);
  }

  if (verbose) {
    auto f = [depth](const auto* p, uint64_t offset) {
      for (uint i = 0; i < depth + 1; ++i) {
        printf("  ");
      }
      if (p->IsMarker()) {
        printf("offset %#" PRIx64 " zero page marker\n", offset);
      } else if (p->IsPage()) {
        vm_page_t* page = p->Page();
        printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "(%c%c%c)\n", offset, page,
               page->paddr(), page->object.cow_left_split ? 'L' : '.',
               page->object.cow_right_split ? 'R' : '.', page->object.always_need ? 'A' : '.');
      } else if (p->IsReference()) {
        const uint64_t cookie = p->Reference().value();
        printf("offset %#" PRIx64 " reference %#" PRIx64 "(%c%c)\n", offset, cookie,
               p->PageOrRefLeftSplit() ? 'L' : '.', p->PageOrRefRightSplit() ? 'R' : '.');
      }
      return ZX_ERR_NEXT;
    };
    page_list_.ForEveryPage(f);
  }
}

VmCowPages::AttributionCounts VmCowPages::AttributedPagesInRangeLocked(uint64_t offset,
                                                                       uint64_t len) const {
  canary_.Assert();

  if (is_hidden_locked()) {
    return AttributionCounts{};
  }

  VmCowPages::AttributionCounts page_counts;
  // TODO: Decide who pages should actually be attribtued to.
  page_list_.ForEveryPageAndGapInRange(
      [&page_counts](const auto* p, uint64_t off) {
        if (p->IsPage()) {
          page_counts.uncompressed++;
        } else if (p->IsReference()) {
          page_counts.compressed++;
        }
        return ZX_ERR_NEXT;
      },
      [this, &page_counts](uint64_t gap_start, uint64_t gap_end) {
        AssertHeld(lock_);

        // If there's no parent, there's no pages to care about. If there is a non-hidden
        // parent, then that owns any pages in the gap, not us.
        if (!parent_) {
          return ZX_ERR_NEXT;
        }
        AssertHeld(parent_->lock_ref());
        if (!parent_->is_hidden_locked()) {
          return ZX_ERR_NEXT;
        }

        // Count any ancestor pages that should be attributed to us in the range. Ideally the whole
        // range gets processed in one attempt, but in order to prevent unbounded stack growth with
        // recursion we instead process partial ranges and recalculate the intermediate results.
        // As a result instead of being O(n) in the number of committed pages it could
        // pathologically become O(nd) where d is our depth in the vmo hierarchy.
        uint64_t off = gap_start;
        while (off < parent_limit_ && off < gap_end) {
          AttributionCounts local_count;
          uint64_t attributed =
              CountAttributedAncestorPagesLocked(off, gap_end - off, &local_count);
          // |CountAttributedAncestorPagesLocked| guarantees that it will make progress.
          DEBUG_ASSERT(attributed > 0);
          off += attributed;
          page_counts += local_count;
        }

        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  return page_counts;
}

uint64_t VmCowPages::CountAttributedAncestorPagesLocked(uint64_t offset, uint64_t size,
                                                        AttributionCounts* count) const
    TA_REQ(lock_) {
  // We need to walk up the ancestor chain to see if there are any pages that should be attributed
  // to this vmo. We attempt operate on the entire range given to us but should we need to query
  // the next parent for a range we trim our operating range. Trimming the range is necessary as
  // we cannot recurse and otherwise have no way to remember where we were up to after processing
  // the range in the parent. The solution then is to return all the way back up to the caller with
  // a partial range and then effectively recompute the meta data at the point we were up to.

  // Note that we cannot stop just because the page_attribution_user_id_ changes. This is because
  // there might still be a forked page at the offset in question which should be attributed to
  // this vmo. Whenever the attribution user id changes while walking up the ancestors, we need
  // to determine if there is a 'closer' vmo in the sibling subtree to which the offset in
  // question can be attributed, or if it should still be attributed to the current vmo.

  DEBUG_ASSERT(offset < parent_limit_);
  const VmCowPages* cur = this;
  AssertHeld(cur->lock_);
  uint64_t cur_offset = offset;
  uint64_t cur_size = size;
  // Count of how many pages we attributed as being owned by this vmo.
  AttributionCounts attributed_ours;
  // Count how much we've processed. This is needed to remember when we iterate up the parent list
  // at an offset.
  uint64_t attributed = 0;
  while (cur_offset < cur->parent_limit_) {
    // For cur->parent_limit_ to be non-zero, it must have a parent.
    DEBUG_ASSERT(cur->parent_);

    const auto parent = cur->parent_.get();
    AssertHeld(parent->lock_);
    uint64_t parent_offset;
    bool overflowed = add_overflow(cur->parent_offset_, cur_offset, &parent_offset);
    DEBUG_ASSERT(!overflowed);                     // vmo creation should have failed
    DEBUG_ASSERT(parent_offset <= parent->size_);  // parent_limit_ prevents this

    const bool left = cur == &parent->left_child_locked();
    const auto& sib = left ? parent->right_child_locked() : parent->left_child_locked();

    // Work out how much of the desired size is actually visible to us in the parent, we just use
    // this to walk the correct amount of the page_list_
    const uint64_t parent_size = ktl::min(cur_size, cur->parent_limit_ - cur_offset);

    // By default we expect to process the entire range, hence our next_size is 0. Should we need to
    // iterate up the stack then these will be set by one of the callbacks.
    uint64_t next_parent_offset = parent_offset + cur_size;
    uint64_t next_size = 0;
    parent->page_list_.ForEveryPageAndGapInRange(
        [&parent, &cur, &attributed_ours, &sib](const auto* p, uint64_t off) {
          AssertHeld(cur->lock_);
          AssertHeld(sib.lock_);
          AssertHeld(parent->lock_);
          if (p->IsMarker()) {
            return ZX_ERR_NEXT;
          }
          if (
              // Page is explicitly owned by us
              (parent->page_attribution_user_id_ == cur->page_attribution_user_id_) ||
              // If page has already been split and we can see it, then we know
              // the sibling subtree can't see the page and thus it should be
              // attributed to this vmo.
              (p->PageOrRefLeftSplit() || p->PageOrRefRightSplit()) ||
              // If the sibling cannot access this page then its ours, otherwise we know there's
              // a vmo in the sibling subtree which is 'closer' to this offset, and to which we will
              // attribute the page to.
              !(sib.parent_offset_ + sib.parent_start_limit_ <= off &&
                off < sib.parent_offset_ + sib.parent_limit_)) {
            if (p->IsPage()) {
              attributed_ours.uncompressed++;
            } else if (p->IsReference()) {
              attributed_ours.compressed++;
            }
          }
          return ZX_ERR_NEXT;
        },
        [&parent, &cur, &next_parent_offset, &next_size, &sib](uint64_t gap_start,
                                                               uint64_t gap_end) {
          // Process a gap in the parent VMO.
          //
          // A gap in the parent VMO doesn't necessarily mean there are no pages
          // in this range: our parent's ancestors may have pages, so we need to
          // walk up the tree to find out.
          //
          // We don't always need to walk the tree though: in this this gap, both this VMO
          // and our sibling VMO will share the same set of ancestor pages. However, the
          // pages will only be accounted to one of the two VMOs.
          //
          // If the parent page_attribution_user_id is the same as us, we need to
          // keep walking up the tree to perform a more accurate count.
          //
          // If the parent page_attribution_user_id is our sibling, however, we
          // can just ignore the overlapping range: pages may or may not exist in
          // the range --- but either way, they would be accounted to our sibling.
          // Instead, we need only walk up ranges not visible to our sibling.
          AssertHeld(cur->lock_);
          AssertHeld(sib.lock_);
          AssertHeld(parent->lock_);
          uint64_t gap_size = gap_end - gap_start;
          if (parent->page_attribution_user_id_ == cur->page_attribution_user_id_) {
            // don't need to consider siblings as we own this range, but we do need to
            // keep looking up the stack to find any actual pages.
            next_parent_offset = gap_start;
            next_size = gap_size;
            return ZX_ERR_STOP;
          }
          // For this entire range we know that the offset is visible to the current vmo, and there
          // are no committed or migrated pages. We need to check though for what portion of this
          // range we should attribute to the sibling. Any range that we can attribute to the
          // sibling we can skip, otherwise we have to keep looking up the stack to see if there are
          // any pages that could be attributed to us.
          uint64_t sib_offset, sib_len;
          if (!GetIntersect(gap_start, gap_size, sib.parent_offset_ + sib.parent_start_limit_,
                            sib.parent_limit_ - sib.parent_start_limit_, &sib_offset, &sib_len)) {
            // No sibling ownership, so need to look at the whole range in the parent to find any
            // pages.
            next_parent_offset = gap_start;
            next_size = gap_size;
            return ZX_ERR_STOP;
          }
          // If the whole range is owned by the sibling, any pages that might be in
          // it won't be accounted to us anyway. Skip the segment.
          if (sib_len == gap_size) {
            DEBUG_ASSERT(sib_offset == gap_start);
            return ZX_ERR_NEXT;
          }

          // Otherwise, inspect the range not visible to our sibling.
          if (sib_offset == gap_start) {
            next_parent_offset = sib_offset + sib_len;
            next_size = gap_end - next_parent_offset;
          } else {
            next_parent_offset = gap_start;
            next_size = sib_offset - gap_start;
          }
          return ZX_ERR_STOP;
        },
        parent_offset, parent_offset + parent_size);
    if (next_size == 0) {
      // If next_size wasn't set then we don't need to keep looking up the chain as we successfully
      // looked at the entire range.
      break;
    }
    // Count anything up to the next starting point as being processed.
    attributed += next_parent_offset - parent_offset;
    // Size should have been reduced by at least the amount we just attributed
    DEBUG_ASSERT(next_size <= cur_size &&
                 cur_size - next_size >= next_parent_offset - parent_offset);

    cur = parent;
    cur_offset = next_parent_offset;
    cur_size = next_size;
  }
  // Exiting the loop means we either ceased finding a relevant parent for the range, or we were
  // able to process the entire range without needing to look up to a parent, in either case we
  // can consider the entire range as attributed.
  //
  // The cur_size can be larger than the value of parent_size from the last loop iteration. This is
  // fine as that range we trivially know has zero pages in it, and therefore has zero pages to
  // determine attributions off.
  attributed += cur_size;

  *count = attributed_ours;
  return attributed;
}

zx_status_t VmCowPages::AddPageLocked(VmPageOrMarker* p, uint64_t offset,
                                      CanOverwriteContent overwrite, VmPageOrMarker* released_page,
                                      bool do_range_update) {
  canary_.Assert();

  if (p->IsPage()) {
    LTRACEF("vmo %p, offset %#" PRIx64 ", page %p (%#" PRIxPTR ")\n", this, offset, p->Page(),
            p->Page()->paddr());
  } else if (p->IsReference()) {
    [[maybe_unused]] const uint64_t cookie = p->Reference().value();
    LTRACEF("vmo %p, offset %#" PRIx64 ", reference %#" PRIx64 "\n", this, offset, cookie);
  } else {
    DEBUG_ASSERT(p->IsMarker());
    LTRACEF("vmo %p, offset %#" PRIx64 ", marker\n", this, offset);
  }

  if (released_page != nullptr) {
    *released_page = VmPageOrMarker::Empty();
  }

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  VmPageOrMarker* page = page_list_.LookupOrAllocate(offset);
  if (!page) {
    return ZX_ERR_NO_MEMORY;
  }

  // We cannot overwrite any kind of content.
  if (overwrite == CanOverwriteContent::None) {
    // An anonymous VMO starts off with all its content set to zero, i.e. at no point can it have
    // absence of content.
    if (!page_source_) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    // This VMO is backed by a page source, so empty slots represent absence of content. Fail if the
    // slot is not empty.
    if (!page->IsEmpty()) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    // This VMO is backed by a page source and the slot is empty. Check if this empty slot
    // represents zero content. For page sources that preserve content (pager backed VMOs), pages
    // starting at the supply_zero_offset_ have an implicit initial content of zero. These pages are
    // not supplied by the user pager, and are instead supplied by the kernel as zero pages. So for
    // pager backed VMOs, we should not overwrite this zero content.
    //
    // TODO(rashaeqbal): Consider replacing supply_zero_offset_ with a single zero range in the page
    // list itself, so that all content resides in the page list. This might require supporting
    // custom sized ranges in the page list; we don't want to pay the cost of individual zero page
    // markers per page or multiple fixed sized zero ranges.
    if (is_source_preserving_page_content() && offset >= supply_zero_offset_) {
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  // We're only permitted to overwrite zero content. This has different meanings based on the
  // whether the VMO is anonymous or is backed by a pager.
  //
  //  * For anonymous VMOs, the initial content for the entire VMO is implicitly all zeroes at the
  //  time of creation. So both zero page markers and empty slots represent zero content. Therefore
  //  the only content type that cannot be overwritten in this case is an actual page.
  //
  //  * For pager backed VMOs, content is either explicitly supplied by the user pager before
  //  supply_zero_offset_, or implicitly supplied as zeros beyond supply_zero_offset_. So zero
  //  content is represented by either zero page markers before supply_zero_offset_ (supplied by the
  //  user pager), or by gaps after supply_zero_offset_ (supplied by the kernel). Therefore the only
  //  content type that cannot be overwritten in this case as well is an actual page.
  if (overwrite == CanOverwriteContent::Zero && page->IsPageOrRef()) {
    // If we have a page source, the page source should be able to validate the page.
    // Note that having a page source implies that any content must be an actual page and so
    // although we return an error for any kind of content, the debug check only gets run for page
    // sources where it will be a real page.
    DEBUG_ASSERT(!page_source_ || page_source_->DebugIsPageOk(page->Page(), offset));
    return ZX_ERR_ALREADY_EXISTS;
  }

  // If the old entry is actual content, release it.
  if (page->IsPageOrRef()) {
    // We should be permitted to overwrite any kind of content (zero or non-zero).
    DEBUG_ASSERT(overwrite == CanOverwriteContent::NonZero);
    // The caller should have passed in an optional to hold the released page.
    DEBUG_ASSERT(released_page != nullptr);
    *released_page = ktl::move(*page);
  }

  // If the new page is an actual page and we have a page source, the page source should be able to
  // validate the page.
  // Note that having a page source implies that any content must be an actual page and so
  // although we return an error for any kind of content, the debug check only gets run for page
  // sources where it will be a real page.
  DEBUG_ASSERT(!p->IsPageOrRef() || !page_source_ ||
               page_source_->DebugIsPageOk(p->Page(), offset));

  // If this is actually a real page, we need to place it into the appropriate queue.
  if (p->IsPage()) {
    vm_page_t* low_level_page = p->Page();
    DEBUG_ASSERT(low_level_page->state() == vm_page_state::OBJECT);
    DEBUG_ASSERT(low_level_page->object.pin_count == 0);
    SetNotPinnedLocked(low_level_page, offset);
  }
  *page = ktl::move(*p);

  if (do_range_update) {
    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);
  }

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return ZX_OK;
}

zx_status_t VmCowPages::AddNewPageLocked(uint64_t offset, vm_page_t* page,
                                         CanOverwriteContent overwrite,
                                         VmPageOrMarker* released_page, bool zero,
                                         bool do_range_update) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));

  InitializeVmPage(page);
  if (zero) {
    ZeroPage(page);
  }

  // Pages being added to pager backed VMOs should have a valid dirty_state before being added to
  // the page list, so that they can be inserted in the correct page queue. New pages start off
  // clean.
  if (is_source_preserving_page_content()) {
    // Only zero pages can be added as new pages to pager backed VMOs.
    DEBUG_ASSERT(zero || IsZeroPage(page));
    UpdateDirtyStateLocked(page, offset, DirtyState::Clean, /*is_pending_add=*/true);
  }

  VmPageOrMarker p = VmPageOrMarker::Page(page);
  zx_status_t status = AddPageLocked(&p, offset, overwrite, released_page, do_range_update);

  if (status != ZX_OK) {
    // Release the page from 'p', as we are returning failure 'page' is still owned by the caller.
    // Store the result in a temporary as we are required to use the result of ReleasePage.
    [[maybe_unused]] vm_page_t* unused = p.ReleasePage();
  }
  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return status;
}

zx_status_t VmCowPages::AddNewPagesLocked(uint64_t start_offset, list_node_t* pages,
                                          CanOverwriteContent overwrite, bool zero,
                                          bool do_range_update) {
  ASSERT(overwrite != CanOverwriteContent::NonZero);
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset));

  uint64_t offset = start_offset;
  while (vm_page_t* p = list_remove_head_type(pages, vm_page_t, queue_node)) {
    // Defer the range change update by passing false as we will do it in bulk at the end if needed.
    zx_status_t status = AddNewPageLocked(offset, p, overwrite, nullptr, zero, false);
    if (status != ZX_OK) {
      // Put the page back on the list so that someone owns it and it'll get free'd.
      list_add_head(pages, &p->queue_node);
      // Decommit any pages we already placed.
      if (offset > start_offset) {
        DecommitRangeLocked(start_offset, offset - start_offset);
      }

      // Free all the pages back as we had ownership of them.
      FreePagesLocked(pages, /*freeing_owned_pages=*/true);
      return status;
    }
    offset += PAGE_SIZE;
  }

  if (do_range_update) {
    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(start_offset, offset - start_offset, RangeChangeOp::Unmap);
  }

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return ZX_OK;
}

bool VmCowPages::IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const {
  DEBUG_ASSERT(page_list_.Lookup(offset)->Page() == page);

  if (page->object.cow_right_split || page->object.cow_left_split) {
    return true;
  }

  if (offset < left_child_locked().parent_offset_ + left_child_locked().parent_start_limit_ ||
      offset >= left_child_locked().parent_offset_ + left_child_locked().parent_limit_) {
    return true;
  }

  if (offset < right_child_locked().parent_offset_ + right_child_locked().parent_start_limit_ ||
      offset >= right_child_locked().parent_offset_ + right_child_locked().parent_limit_) {
    return true;
  }

  return false;
}

zx_status_t VmCowPages::CloneCowPageLocked(uint64_t offset, list_node_t* alloc_list,
                                           VmCowPages* page_owner, vm_page_t* page,
                                           uint64_t owner_offset, LazyPageRequest* page_request,
                                           vm_page_t** out_page) {
  DEBUG_ASSERT(page != vm_get_zero_page());
  DEBUG_ASSERT(parent_);
  DEBUG_ASSERT(page_request);

  // To avoid the need for rollback logic on allocation failure, we start the forking
  // process from the root-most vmo and work our way towards the leaf vmo. This allows
  // us to maintain the hidden vmo invariants through the whole operation, so that we
  // can stop at any point.
  //
  // To set this up, walk from the leaf to |page_owner|, and keep track of the
  // path via |stack_.dir_flag|.
  VmCowPages* cur = this;
  do {
    AssertHeld(cur->lock_);
    VmCowPages* next = cur->parent_.get();
    // We can't make COW clones of physical vmos, so this can only happen if we
    // somehow don't find |page_owner| in the ancestor chain.
    DEBUG_ASSERT(next);
    AssertHeld(next->lock_);

    next->stack_.dir_flag = &next->left_child_locked() == cur ? StackDir::Left : StackDir::Right;
    if (next->stack_.dir_flag == StackDir::Right) {
      DEBUG_ASSERT(&next->right_child_locked() == cur);
    }
    cur = next;
  } while (cur != page_owner);
  uint64_t cur_offset = owner_offset;

  // |target_page| is the page we're considering for migration. Cache it
  // across loop iterations.
  vm_page_t* target_page = page;

  zx_status_t alloc_status = ZX_OK;

  // As long as we're simply migrating |page|, there's no need to update any vmo mappings, since
  // that means the other side of the clone tree has already covered |page| and the current side
  // of the clone tree will still see |page|. As soon as we insert a new page, we'll need to
  // update all mappings at or below that level.
  bool skip_range_update = true;
  do {
    // |target_page| is always located at in |cur| at |cur_offset| at the start of the loop.
    VmCowPages* target_page_owner = cur;
    AssertHeld(target_page_owner->lock_);
    uint64_t target_page_offset = cur_offset;

    cur = cur->stack_.dir_flag == StackDir::Left ? &cur->left_child_locked()
                                                 : &cur->right_child_locked();
    DEBUG_ASSERT(cur_offset >= cur->parent_offset_);
    cur_offset -= cur->parent_offset_;

    if (target_page_owner->IsUniAccessibleLocked(target_page, target_page_offset)) {
      // If the page we're covering in the parent is uni-accessible, then we
      // can directly move the page.

      // Assert that we're not trying to split the page the same direction two times. Either
      // some tracking state got corrupted or a page in the subtree we're trying to
      // migrate to got improperly migrated/freed. If we did this migration, then the
      // opposite subtree would lose access to this page.
      DEBUG_ASSERT(!(target_page_owner->stack_.dir_flag == StackDir::Left &&
                     target_page->object.cow_left_split));
      DEBUG_ASSERT(!(target_page_owner->stack_.dir_flag == StackDir::Right &&
                     target_page->object.cow_right_split));
      // For now, we won't see a loaned page here.
      DEBUG_ASSERT(!target_page->is_loaned());

      target_page->object.cow_left_split = 0;
      target_page->object.cow_right_split = 0;
      VmPageOrMarker removed = target_page_owner->page_list_.RemoveContent(target_page_offset);
      // We know this is a true page since it is just our |target_page|, which is a true page.
      vm_page* removed_page = removed.ReleasePage();
      pmm_page_queues()->Remove(removed_page);
      DEBUG_ASSERT(removed_page == target_page);
    } else {
      // Otherwise we need to fork the page.  The page has no writable mappings so we don't need to
      // remove write or unmap before copying the contents.
      vm_page_t* cover_page;
      alloc_status =
          AllocateCopyPage(pmm_alloc_flags_, page->paddr(), alloc_list, page_request, &cover_page);
      if (alloc_status != ZX_OK) {
        break;
      }

      // We're going to cover target_page with cover_page, so set appropriate split bit.
      if (target_page_owner->stack_.dir_flag == StackDir::Left) {
        target_page->object.cow_left_split = 1;
        DEBUG_ASSERT(target_page->object.cow_right_split == 0);
      } else {
        target_page->object.cow_right_split = 1;
        DEBUG_ASSERT(target_page->object.cow_left_split == 0);
      }
      target_page = cover_page;

      skip_range_update = false;
    }

    // Skip the automatic range update so we can do it ourselves more efficiently.
    VmPageOrMarker add_page = VmPageOrMarker::Page(target_page);
    zx_status_t status =
        cur->AddPageLocked(&add_page, cur_offset, CanOverwriteContent::Zero, nullptr, false);
    DEBUG_ASSERT_MSG(status == ZX_OK, "AddPageLocked returned %d\n", status);

    if (!skip_range_update) {
      if (cur != this) {
        // In this case, cur is a hidden vmo and has no direct mappings. Also, its
        // descendents along the page stack will be dealt with by subsequent iterations
        // of this loop. That means that any mappings that need to be touched now are
        // owned by the children on the opposite side of stack_.dir_flag.
        VmCowPages& other = cur->stack_.dir_flag == StackDir::Left ? cur->right_child_locked()
                                                                   : cur->left_child_locked();
        AssertHeld(other.lock_);
        RangeChangeList list;
        other.RangeChangeUpdateFromParentLocked(cur_offset, PAGE_SIZE, &list);
        RangeChangeUpdateListLocked(&list, RangeChangeOp::Unmap);
      } else {
        // In this case, cur is the last vmo being changed, so update its whole subtree.
        DEBUG_ASSERT(offset == cur_offset);
        RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);
      }
    }
  } while (cur != this);
  DEBUG_ASSERT(alloc_status != ZX_OK || cur_offset == offset);

  if (unlikely(alloc_status != ZX_OK)) {
    *out_page = nullptr;
    return alloc_status;
  } else {
    *out_page = target_page;
    return ZX_OK;
  }
}

zx_status_t VmCowPages::CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* freed_list,
                                                 VmCowPages* page_owner, vm_page_t* page,
                                                 uint64_t owner_offset,
                                                 LazyPageRequest* page_request) {
  DEBUG_ASSERT(parent_);

  // Ensure we have a slot as we'll need it later.
  const VmPageOrMarker* slot = page_list_.LookupOrAllocate(offset);

  if (!slot) {
    return ZX_ERR_NO_MEMORY;
  }

  // We cannot be forking a page to here if there's already something.
  DEBUG_ASSERT(slot->IsEmpty());

  DEBUG_ASSERT(!page_source_ || page_source_->DebugIsPageOk(page, offset));

  // Need to make sure the page is duplicated as far as our parent. Then we can pretend
  // that we have forked it into us by setting the marker.
  AssertHeld(parent_->lock_);
  if (page_owner != parent_.get()) {
    // Do not pass our freed_list here as this wants an alloc_list to allocate from.
    zx_status_t result = parent_->CloneCowPageLocked(offset + parent_offset_, nullptr, page_owner,
                                                     page, owner_offset, page_request, &page);
    if (result != ZX_OK) {
      return result;
    }
  }

  bool left = this == &(parent_->left_child_locked());
  // Page is in our parent. Check if its uni accessible, if so we can free it.
  if (parent_->IsUniAccessibleLocked(page, offset + parent_offset_)) {
    // Make sure we didn't already merge the page in this direction.
    DEBUG_ASSERT(!(left && page->object.cow_left_split));
    DEBUG_ASSERT(!(!left && page->object.cow_right_split));
    // We are going to be inserting removed pages into a shared free list. So make sure the parent
    // did not have a page source that was handling frees which would require additional work on the
    // owned pages on top of a simple free to the PMM.
    DEBUG_ASSERT(!parent_->is_source_handling_free_locked());
    // We know this is a true page since it is just our target |page|.
    vm_page* removed = parent_->page_list_.RemoveContent(offset + parent_offset_).ReleasePage();
    DEBUG_ASSERT(removed == page);
    pmm_page_queues()->Remove(removed);
    DEBUG_ASSERT(!list_in_list(&removed->queue_node));
    list_add_tail(freed_list, &removed->queue_node);
  } else {
    if (left) {
      page->object.cow_left_split = 1;
    } else {
      page->object.cow_right_split = 1;
    }
  }
  // Insert the zero marker.
  VmPageOrMarker new_marker = VmPageOrMarker::Marker();
  // We know that the slot is empty, so we know we won't be overwriting an actual page.
  // We expect the caller to update any mappings.
  zx_status_t status = AddPageLocked(&new_marker, offset, CanOverwriteContent::Zero, nullptr,
                                     /*do_range_update=*/false);
  // Absent bugs, AddPageLocked() can only return ZX_ERR_NO_MEMORY, but that failure can only
  // occur if we had to allocate a slot in the page list. Since we allocated a slot above, we
  // know that can't be the case.
  DEBUG_ASSERT(status == ZX_OK);
  return ZX_OK;
}

VmPageOrMarkerRef VmCowPages::FindInitialPageContentLocked(uint64_t offset, VmCowPages** owner_out,
                                                           uint64_t* owner_offset_out,
                                                           uint64_t* owner_length) {
  // Search up the clone chain for any committed pages. cur_offset is the offset
  // into cur we care about. The loop terminates either when that offset contains
  // a committed page or when that offset can't reach into the parent.
  VmPageOrMarkerRef page;
  VmCowPages* cur = this;
  AssertHeld(cur->lock_);
  uint64_t cur_offset = offset;
  while (cur_offset < cur->parent_limit_) {
    VmCowPages* parent = cur->parent_.get();
    // If there's no parent, then parent_limit_ is 0 and we'll never enter the loop
    DEBUG_ASSERT(parent);
    AssertHeld(parent->lock_ref());

    uint64_t parent_offset;
    bool overflowed = add_overflow(cur->parent_offset_, cur_offset, &parent_offset);
    ASSERT(!overflowed);
    if (parent_offset >= parent->size_) {
      // The offset is off the end of the parent, so cur is the VmObjectPaged
      // which will provide the page.
      break;
    }
    if (owner_length) {
      // Before we walk up, need to check to see if there's any forked pages that require us to
      // restrict the owner length. Additionally need to restrict the owner length to the actual
      // parent limit.
      *owner_length = ktl::min(*owner_length, cur->parent_limit_ - cur_offset);
      cur->page_list_.ForEveryPageInRange(
          [owner_length, cur_offset](const VmPageOrMarker*, uint64_t off) {
            *owner_length = off - cur_offset;
            return ZX_ERR_STOP;
          },
          cur_offset, cur_offset + *owner_length);
    }

    cur = parent;
    cur_offset = parent_offset;
    VmPageOrMarkerRef p = cur->page_list_.LookupMutable(parent_offset);
    if (p && !p->IsEmpty()) {
      page = p;
      break;
    }
  }

  *owner_out = cur;
  *owner_offset_out = cur_offset;

  return page;
}

void VmCowPages::UpdateDirtyStateLocked(vm_page_t* page, uint64_t offset, DirtyState dirty_state,
                                        bool is_pending_add) {
  ASSERT(page);
  ASSERT(is_source_preserving_page_content());

  // If the page is not pending being added to the page list, it should have valid object info.
  DEBUG_ASSERT(is_pending_add || page->object.get_object() == this);
  DEBUG_ASSERT(is_pending_add || page->object.get_page_offset() == offset);

  // If the page is Dirty or AwaitingClean, it should not be loaned.
  DEBUG_ASSERT(!(is_page_dirty(page) || is_page_awaiting_clean(page)) || !page->is_loaned());

  // Perform state-specific checks and actions. We will finally update the state below.
  switch (dirty_state) {
    case DirtyState::Clean:
      // If the page is not in the process of being added, we can only see a transition to Clean
      // from AwaitingClean.
      ASSERT(is_pending_add || is_page_awaiting_clean(page));

      // If we are expecting a pending Add[New]PageLocked, we can defer updating the page queue.
      if (!is_pending_add) {
        // Move to evictable pager backed queue to start tracking age information.
        pmm_page_queues()->MoveToPagerBacked(page);
      }
      break;
    case DirtyState::Dirty:
      // If the page is not in the process of being added, we can only see a transition to Dirty
      // from Clean or AwaitingClean.
      ASSERT(is_pending_add || (is_page_clean(page) || is_page_awaiting_clean(page)));

      // A loaned page cannot be marked Dirty as loaned pages are reclaimed by eviction; Dirty pages
      // cannot be evicted.
      DEBUG_ASSERT(!page->is_loaned());

      // If we are expecting a pending Add[New]PageLocked, we can defer updating the page queue.
      if (!is_pending_add) {
        // Move the page to the Dirty queue, which does not track page age. While the page is in the
        // Dirty queue, age information is not required (yet). It will be required when the page
        // becomes Clean (and hence evictable) again, at which point it will get moved to the MRU
        // pager backed queue and will age as normal.
        // TODO(rashaeqbal): We might want age tracking for the Dirty queue in the future when the
        // kernel generates writeback pager requests.
        pmm_page_queues()->MoveToPagerBackedDirty(page);
      }
      break;
    case DirtyState::AwaitingClean:
      // A newly added page cannot start off as AwaitingClean.
      ASSERT(!is_pending_add);
      // A pinned page will be kept Dirty as long as it is pinned.
      //
      // Note that there isn't a similar constraint when setting the Clean state as it is possible
      // to pin a page for read after it has been marked AwaitingClean. Since it is a pinned read it
      // does not need to dirty the page. So when the writeback is done it can transition from
      // AwaitingClean -> Clean with a non-zero pin count.
      //
      // It is also possible for us to observe an intermediate pin count for a write-pin that has
      // not fully completed yet, as we will only attempt to dirty pages after pinning them. So it
      // is possible for a thread to be waiting on a DIRTY request on a pinned page, while a racing
      // writeback transitions the page from AwaitingClean -> Clean with a non-zero pin count.
      ASSERT(page->object.pin_count == 0);
      // We can only transition to AwaitingClean from Dirty.
      ASSERT(is_page_dirty(page));
      // A loaned page cannot be marked AwaitingClean as loaned pages are reclaimed by eviction;
      // AwaitingClean pages cannot be evicted.
      DEBUG_ASSERT(!page->is_loaned());
      // No page queue update. Leave the page in the Dirty queue for now as it is not clean yet;
      // it will be moved out on WritebackEnd.
      DEBUG_ASSERT(pmm_page_queues()->DebugPageIsPagerBackedDirty(page));
      break;
    default:
      ASSERT(false);
  }
  page->object.dirty_state = static_cast<uint8_t>(dirty_state) & VM_PAGE_OBJECT_DIRTY_STATES_MASK;
}

zx_status_t VmCowPages::PrepareForWriteLocked(uint64_t offset, uint64_t len,
                                              LazyPageRequest* page_request,
                                              uint64_t* dirty_len_out) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);

    // PagedParentOfSliceLocked will walk all of the way up the VMO hierarchy
    // until it hits a non-slice VMO.  This guarantees that we should only ever
    // recurse once instead of an unbound number of times.  DEBUG_ASSERT this so
    // that we don't actually end up with unbound recursion just in case the
    // property changes.
    DEBUG_ASSERT(!parent->is_slice_locked());

    return parent->PrepareForWriteLocked(offset + parent_offset, len, page_request, dirty_len_out);
  }

  DEBUG_ASSERT(page_source_);
  DEBUG_ASSERT(is_source_preserving_page_content());

  uint64_t dirty_len = 0;
  const uint64_t start_offset = offset;
  const uint64_t end_offset = offset + len;

  // If the VMO does not require us to trap dirty transitions, simply mark the pages dirty, and move
  // them to the dirty page queue. Do this only for the first consecutive run of committed pages
  // within the range starting at offset. Any absent pages will need to be provided by the page
  // source, which might fail and terminate the lookup early. Any zero page markers might need to be
  // forked, which can fail too. Only mark those pages dirty that the lookup is guaranteed to return
  // successfully.
  if (!page_source_->ShouldTrapDirtyTransitions()) {
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [this, &dirty_len, start_offset](const VmPageOrMarker* p, uint64_t off) {
          if (p->IsMarker()) {
            // Found a marker. End the traversal.
            return ZX_ERR_STOP;
          }
          // VMOs with a page source will never have compressed references, so this should be a
          // real page.
          DEBUG_ASSERT(p->IsPage());
          vm_page_t* page = p->Page();
          DEBUG_ASSERT(is_page_dirty_tracked(page));
          DEBUG_ASSERT(page->object.get_object() == this);
          DEBUG_ASSERT(page->object.get_page_offset() == off);

          // End the traversal if we encounter a loaned page. We reclaim loaned pages by evicting
          // them, and dirty pages cannot be evicted.
          if (page->is_loaned()) {
            // If this is a loaned page, it should be clean.
            DEBUG_ASSERT(is_page_clean(page));
            return ZX_ERR_STOP;
          }
          DEBUG_ASSERT(!page->is_loaned());

          // Mark the page dirty.
          if (!is_page_dirty(page)) {
            AssertHeld(lock_);
            UpdateDirtyStateLocked(page, off, DirtyState::Dirty);
          }
          // The page was either already dirty, or we just marked it dirty. Proceed to the next one.
          DEBUG_ASSERT(start_offset + dirty_len == off);
          dirty_len += PAGE_SIZE;
          return ZX_ERR_NEXT;
        },
        [](uint64_t start, uint64_t end) {
          // We found a gap. End the traversal.
          return ZX_ERR_STOP;
        },
        start_offset, end_offset);
    // We don't expect a failure from the traversal.
    DEBUG_ASSERT(status == ZX_OK);

    *dirty_len_out = dirty_len;
    VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
    return ZX_OK;
  }

  // Otherwise, generate a DIRTY page request for pages in the range which need to transition to
  // Dirty. The eligibility criteria is different depending on which side of supply_zero_offset_ the
  // page lies.
  //
  //  - For pages before supply_zero_offset_:
  //  Find a contiguous run of non-Dirty pages (committed pages as well as zero page markers).
  //  For the purpose of generating DIRTY requests, both Clean and AwaitingClean pages are
  //  considered equivalent. This is because pages that are in AwaitingClean will need another
  //  acknowledgment from the user pager before they can be made Dirty (the filesystem might need to
  //  reserve additional space for them etc.).
  //
  //  - For pages at and after supply_zero_offset_:
  //    - Any gaps are implicit zero pages, i.e. the kernel supplies zero pages when they are
  //    accessed. Since these pages are not supplied by the user pager via zx_pager_supply_pages, we
  //    will need to wait on a DIRTY request before the gap can be replaced by an actual page for
  //    writing (the filesystem might need to reserve additional space).
  //    - There can exist actual pages beyond supply_zero_offset_ from previous writes, but these
  //    will either be Dirty or AwaitingClean, since we cannot mark a page Clean beyond
  //    supply_zero_offset_ without also advancing supply_zero_offset_ after the Clean page. This is
  //    because the range after supply_zero_offset_ is supplied by the kernel, not the user pager,
  //    so if we were to Clean a page beyond supply_zero_offset_, it might get evicted, and then
  //    incorrectly supplied by the kernel as a zero page. It is possible for pages to be in
  //    AwaitingClean if the user pager is attempting to write them back, in which case a future
  //    write to the page is treated the same as before supply_zero_offset_. It must be trapped so
  //    that the filesystem can acknowledge it again (it might need to reserve additional space
  //    again).
  uint64_t pages_to_dirty_len = 0;

  // Helper lambda used in the page list traversal below. Try to add page at |dirty_page_offset| to
  // the run of dirty pages being tracked. Return codes are the same as those used by
  // VmPageList::ForEveryPageAndGapInRange to continue or terminate traversal.
  auto accumulate_dirty_page = [&pages_to_dirty_len, &dirty_len,
                                start_offset](uint64_t dirty_page_offset) -> zx_status_t {
    // Bail if we were tracking a non-zero run of pages to be dirtied as we cannot extend
    // pages_to_dirty_len anymore.
    if (pages_to_dirty_len > 0) {
      return ZX_ERR_STOP;
    }
    // Append the page to the dirty range being tracked if it immediately follows it.
    if (start_offset + dirty_len == dirty_page_offset) {
      dirty_len += PAGE_SIZE;
      return ZX_ERR_NEXT;
    }
    // Otherwise we cannot accumulate any more contiguous dirty pages.
    return ZX_ERR_STOP;
  };

  // Helper lambda used in the page list traversal below. Try to add pages in the range
  // [to_dirty_start, to_dirty_end) to the run of to-be-dirtied pages being tracked. Return codes
  // are the same as those used by VmPageList::ForEveryPageAndGapInRange to continue or terminate
  // traversal.
  auto accumulate_pages_to_dirty = [&pages_to_dirty_len, &dirty_len, start_offset](
                                       uint64_t to_dirty_start,
                                       uint64_t to_dirty_end) -> zx_status_t {
    // Bail if we were already accumulating a non-zero run of Dirty pages.
    if (dirty_len > 0) {
      return ZX_ERR_STOP;
    }
    // Append the pages to the range being tracked if they immediately follow it.
    if (start_offset + pages_to_dirty_len == to_dirty_start) {
      pages_to_dirty_len += (to_dirty_end - to_dirty_start);
      return ZX_ERR_NEXT;
    }
    // Otherwise we cannot accumulate any more contiguous to-dirty pages.
    return ZX_ERR_STOP;
  };

  // First consider the portion of the range that ends before supply_zero_offset_.
  // We don't have a range to consider here if offset was greater than supply_zero_offset_.
  if (start_offset < supply_zero_offset_) {
    const uint64_t end = ktl::min(supply_zero_offset_, end_offset);
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&accumulate_dirty_page, &accumulate_pages_to_dirty, this](const VmPageOrMarker* p,
                                                                   uint64_t off) {
          if (p->IsPage()) {
            vm_page_t* page = p->Page();
            DEBUG_ASSERT(is_page_dirty_tracked(page));
            // VMOs that trap dirty transitions should not have loaned pages.
            DEBUG_ASSERT(!page->is_loaned());
            // Page is already dirty. Try to add it to the dirty run.
            if (is_page_dirty(page)) {
              return accumulate_dirty_page(off);
            }
            // If the page is clean, mark it accessed to grant it some protection from eviction
            // until the pager has a chance to respond to the DIRTY request.
            if (is_page_clean(page)) {
              AssertHeld(lock_);
              UpdateOnAccessLocked(page, VMM_PF_FLAG_SW_FAULT);
            }
          }
          DEBUG_ASSERT(!p->IsReference());
          // This is a either a zero page marker (which represents a clean zero page) or a committed
          // page which is not already Dirty. Try to add it to the range of pages to be dirtied.
          return accumulate_pages_to_dirty(off, off + PAGE_SIZE);
        },
        [](uint64_t start, uint64_t end) {
          // We found a gap. End the traversal.
          return ZX_ERR_STOP;
        },
        start_offset, end);

    // We don't expect an error from the traversal above. If an incompatible contiguous page or
    // a gap is encountered, we will simply terminate early.
    DEBUG_ASSERT(status == ZX_OK);
  }

  // Now consider the portion of the range that starts at/after supply_zero_offset_, and see if we
  // can extend an already existing to-dirty range, or start a new one. [offset, offset + len) might
  // have fallen entirely before supply_zero_offset_, in which case we have no remaining portion to
  // consider here.
  if (supply_zero_offset_ < end_offset) {
    const uint64_t start = ktl::max(start_offset, supply_zero_offset_);
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&accumulate_dirty_page, &accumulate_pages_to_dirty](const VmPageOrMarker* p,
                                                             uint64_t off) {
          // We can only find un-Clean committed pages beyond supply_zero_offset_. There can be no
          // markers as well as they represent Clean zero pages.
          ASSERT(p->IsPage());
          vm_page_t* page = p->Page();
          ASSERT(is_page_dirty_tracked(page));
          ASSERT(!is_page_clean(page));
          DEBUG_ASSERT(!page->is_loaned());

          // Page is already dirty. Try to add it to the dirty run.
          if (is_page_dirty(page)) {
            return accumulate_dirty_page(off);
          }

          // This page was not Dirty, the only other state a page beyond supply_zero_offset_ could
          // be in is AwaitingClean.
          ASSERT(is_page_awaiting_clean(page));
          // Try to add this page to the range of pages to be dirtied.
          return accumulate_pages_to_dirty(off, off + PAGE_SIZE);
        },
        [&accumulate_pages_to_dirty](uint64_t start, uint64_t end) {
          // We need to request a Dirty transition for the gap. Try to add it to the range of pages
          // to be dirtied.
          return accumulate_pages_to_dirty(start, end);
        },
        start, end_offset);

    // We don't expect an error from the traversal above. If an already dirty page or a
    // non-contiguous page/gap is encountered, we will simply terminate early.
    DEBUG_ASSERT(status == ZX_OK);
  }

  // We should either have found dirty pages or pages that need to be dirtied, but not both.
  DEBUG_ASSERT(dirty_len == 0 || pages_to_dirty_len == 0);
  // Check that dirty_len and pages_to_dirty_len both specify valid ranges.
  DEBUG_ASSERT(start_offset + dirty_len <= end_offset);
  DEBUG_ASSERT(pages_to_dirty_len == 0 || start_offset + pages_to_dirty_len <= end_offset);

  *dirty_len_out = dirty_len;

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());

  // No pages need to transition to Dirty.
  if (pages_to_dirty_len == 0) {
    return ZX_OK;
  }

  // Found a contiguous run of pages that need to transition to Dirty. There might be more such
  // pages later in the range, but we will come into this call again for them via another
  // LookupPagesLocked after the waiting caller is unblocked for this range.
  AssertHeld(paged_ref_->lock_ref());
  VmoDebugInfo vmo_debug_info = {.vmo_ptr = reinterpret_cast<uintptr_t>(paged_ref_),
                                 .vmo_id = paged_ref_->user_id_locked()};
  zx_status_t status = page_source_->RequestDirtyTransition(page_request->get(), start_offset,
                                                            pages_to_dirty_len, vmo_debug_info);
  // The page source will never succeed synchronously.
  DEBUG_ASSERT(status != ZX_OK);
  return status;
}

void VmCowPages::UpdateOnAccessLocked(vm_page_t* page, uint pf_flags) {
  // We only care about updating on access if we can reclaim pages, which if reclamation is limited
  // to pager backed can be skipped if eviction isn't possible.
  if (PageQueues::ReclaimIsOnlyPagerBacked() && !can_evict()) {
    return;
  }

  // Don't make the page accessed for hardware faults. These accesses, if any actually end up
  // happening, will be detected by the accessed bits in the page tables.
  // For non hardware faults, the kernel might use the page directly through the physmap, which will
  // not cause accessed information to be updated and so we consider it accessed at this point.
  if (pf_flags & VMM_PF_FLAG_HW_FAULT) {
    return;
  }

  pmm_page_queues()->MarkAccessed(page);
}

// Looks up the page at the requested offset, faulting it in if requested and necessary.  If
// this VMO has a parent and the requested page isn't found, the parent will be searched.
//
// Both VMM_PF_FLAG_HW_FAULT and VMM_PF_FLAG_SW_FAULT are treated identically with respect to the
// values that get returned, they only differ with respect to internal meta-data that gets updated
// different. If SW or HW fault then unless there is some other error condition, a page of some kind
// will always be returned, performing allocations as required.
// The rules for non faults are:
//  * A reference to the zero page will never be returned, be it because reading from an uncommitted
//    offset or from a marker. Uncommitted offsets and markers will always result in
//    ZX_ERR_NOT_FOUND
//  * Writes to real committed pages (i.e. non markers) in parent VMOs will cause a copy-on-write
//    fork to be allocated into this VMO and returned.
// This means that
//  * Reads or writes to committed real (non marker) pages in this VMO will always succeed.
//  * Reads to committed real (non marker) pages in parents will succeed
//  * Writes to real pages in parents will trigger a COW fork and succeed
//  * All other cases, that is reads or writes to markers in this VMO or the parent and uncommitted
//    offsets, will not trigger COW forks or allocations and will fail.
//
// |alloc_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |alloc_list| is a non-empty list, faulting in was requested,
// and offset is in range.
zx_status_t VmCowPages::LookupPagesLocked(uint64_t offset, uint pf_flags,
                                          DirtyTrackingAction mark_dirty, uint64_t max_out_pages,
                                          list_node* alloc_list, LazyPageRequest* page_request,
                                          LookupInfo* out) {
  VM_KTRACE_DURATION(2, "VmCowPages::LookupPagesLocked", page_attribution_user_id_, offset);
  canary_.Assert();
  DEBUG_ASSERT(!is_hidden_locked());
  DEBUG_ASSERT(out);
  DEBUG_ASSERT(max_out_pages > 0);
  DEBUG_ASSERT(page_request || !(pf_flags & VMM_PF_FLAG_FAULT_MASK));
  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // This vmo was discarded and has not been locked yet after the discard. Do not return any pages.
  if (discardable_state_ == DiscardableState::kDiscarded) {
    return ZX_ERR_NOT_FOUND;
  }

  offset = ROUNDDOWN(offset, PAGE_SIZE);

  // Trim the number of output pages to the size of this VMO. This ensures any range calculation
  // can never overflow.
  max_out_pages = ktl::min(static_cast<uint64_t>(max_out_pages), ((size_ - offset) / PAGE_SIZE));

  if (is_slice_locked()) {
    uint64_t parent_offset = 0;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->LookupPagesLocked(offset + parent_offset, pf_flags, mark_dirty, max_out_pages,
                                     alloc_list, page_request, out);
  }

  // Ensure we're adding pages to an empty list so we don't risk overflowing it.
  out->num_pages = 0;

  // Helper to find contiguous runs of pages in a page list and add them to the output pages.
  auto collect_pages = [out, pf_flags](VmCowPages* cow, uint64_t offset, uint64_t max_len) {
    DEBUG_ASSERT(max_len > 0);

    AssertHeld(cow->lock_);
    cow->page_list_.ForEveryPageAndGapInRange(
        [out, cow, pf_flags](const VmPageOrMarker* page, uint64_t off) {
          // Only pre-map in ready content pages.
          if (!page->IsPage()) {
            return ZX_ERR_STOP;
          }
          vm_page_t* p = page->Page();
          AssertHeld(cow->lock_);
          cow->UpdateOnAccessLocked(p, pf_flags);
          out->add_page(p->paddr());
          return ZX_ERR_NEXT;
        },
        [](uint64_t start, uint64_t end) {
          // This is a gap, and we never want to pre-map in zero pages.
          return ZX_ERR_STOP;
        },
        offset, CheckedAdd(offset, max_len));
  };

  // We perform an exact Lookup and not something more fancy as a trade off between three scenarios
  //  * Page is in this page list and max_out_pages == 1
  //  * Page is not in this page list
  //  * Page is in this page list and max_out_pages > 1
  // In the first two cases an exact Lookup is the most optimal choice, and in the third scenario
  // although we have to re-walk the page_list_ 'needlessly', we should somewhat amortize it by the
  // fact we return multiple pages.
  VmPageOrMarkerRef page_or_mark = page_list_.LookupMutable(offset);
  if (page_or_mark && page_or_mark->IsPageOrRef()) {
    if (page_or_mark->IsReference()) {
      // Must be faulting in order to turn this reference into a real page.
      if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
        return ZX_ERR_NOT_FOUND;
      }
      zx_status_t status = ReplaceReferenceWithPageLocked(page_or_mark, offset, page_request);
      if (status != ZX_OK) {
        return status;
      }
    }
    // This is the common case where we have the page and don't need to do anything more, so
    // return it straight away, collecting any additional pages if possible.
    vm_page_t* p = page_or_mark->Page();

    // If we're writing to a root VMO backed by a user pager, i.e. a VMO whose page source preserves
    // page contents, we might need to mark pages Dirty so that they can be written back later. This
    // is the only path that can result in a write to such a page; if the page was not present, we
    // would have already blocked on a read request the first time, and ended up here when
    // unblocked, at which point the page would be present.
    uint64_t dirty_len = 0;
    if ((pf_flags & VMM_PF_FLAG_WRITE) && is_source_preserving_page_content() &&
        mark_dirty == DirtyTrackingAction::DirtyAllPagesOnWrite) {
      // If this page was loaned, it should be replaced with a non-loaned page, so that we can make
      // progress with marking pages dirty. PrepareForWriteLocked terminates its page walk when it
      // encounters a loaned page; loaned pages are reclaimed by evicting them and we cannot evict
      // dirty pages.
      if (p->is_loaned()) {
        DEBUG_ASSERT(is_page_clean(p));
        DEBUG_ASSERT(page_request);
        zx_status_t status = ReplacePageLocked(p, offset, /*with_loaned=*/false, &p, page_request);
        if (status != ZX_OK) {
          return status;
        }
      }
      DEBUG_ASSERT(!p->is_loaned());

      // Pass in max_out_pages for the requested length. If the VMO traps dirty transitions, this
      // will allow extending the DIRTY request to also include other consecutive markers /
      // non-dirty pages in the entire lookup range. This is an optimization to reduce the number of
      // DIRTY page requests generated overall.
      zx_status_t status =
          PrepareForWriteLocked(offset, max_out_pages * PAGE_SIZE, page_request, &dirty_len);
      if (status != ZX_OK) {
        // We were not able to dirty any pages.
        DEBUG_ASSERT(dirty_len == 0);
        // No pages to return.
        out->num_pages = 0;
        return status;
      }

      // PrepareForWriteLocked was successful, so we should have some dirty pages, and they should
      // be within the requested range.
      DEBUG_ASSERT(dirty_len >= PAGE_SIZE);
      DEBUG_ASSERT(dirty_len <= max_out_pages * PAGE_SIZE);
      // PrepareForWriteLocked returned successfully, so we know that pages in the range [offset,
      // offset + dirty_len) have been dirtied. We need to clip the maximum range collect_pages
      // iterates over below to dirty_len, so that only pages that have been prepared for the write
      // (by marking dirty) are returned.
      max_out_pages = dirty_len / PAGE_SIZE;
    }

    // This is writable if either of these conditions is true:
    // 1) This is a write fault.
    // 2) This is a read fault and we do not need to do dirty tracking, i.e. it is fine to retain
    // the write permission on mappings since we don't need to generate a permission fault. We only
    // need to dirty track pages owned by a root user-pager-backed VMO, i.e. a VMO with a page
    // source that preserves page contents.
    out->writable = pf_flags & VMM_PF_FLAG_WRITE || !is_source_preserving_page_content();

    UpdateOnAccessLocked(p, pf_flags);
    out->add_page(p->paddr());
    if (max_out_pages > 1) {
      collect_pages(this, offset + PAGE_SIZE, (max_out_pages - 1) * PAGE_SIZE);
    }

    // If dirtiness was applicable i.e. we reached here after calling PrepareForWriteLocked, we
    // should have dirtied exactly the same number of pages that is being returned.
    DEBUG_ASSERT_MSG(dirty_len == 0 || dirty_len == out->num_pages * PAGE_SIZE,
                     "dirty pages %zu, looked up pages %zu\n", dirty_len / PAGE_SIZE,
                     out->num_pages);

    return ZX_OK;
  }

  // The only time we will say something is writable when the fault is a read is if the page is
  // already in this VMO. That scenario is the above if block, and so if we get here then writable
  // mirrors the fault flag.
  const bool writing = (pf_flags & VMM_PF_FLAG_WRITE) != 0;
  out->writable = writing;

  // If we are reading we track the visible length of pages in the owner. We don't bother tracking
  // this for writing, since when writing we will fork the page into ourselves anyway.
  uint64_t visible_length = writing ? PAGE_SIZE : PAGE_SIZE * max_out_pages;
  // Get content from parent if available, otherwise accept we are the owner of the yet to exist
  // page.
  VmCowPages* page_owner = nullptr;
  uint64_t owner_offset = 0;
  if ((!page_or_mark || page_or_mark->IsEmpty()) && parent_) {
    // Pass nullptr if visible_length is PAGE_SIZE to allow the lookup to short-circuit the length
    // calculation, as the calculation involves additional page lookups at every level.
    page_or_mark = FindInitialPageContentLocked(
        offset, &page_owner, &owner_offset, visible_length > PAGE_SIZE ? &visible_length : nullptr);
  } else {
    page_owner = this;
    owner_offset = offset;
  }

  // At this point we might not have an actual page, but we should at least have a notional owner.
  DEBUG_ASSERT(page_owner);

  __UNUSED char pf_string[5];
  LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
          vmm_pf_flags_to_string(pf_flags, pf_string));

  // If we don't have a real page, and we're not sw or hw faulting in the page, return not found.
  if ((!page_or_mark || !page_or_mark->IsPage()) && (pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  // We need to turn this potential page or marker into a real vm_page_t. This means failing cases
  // that we cannot handle, determining whether we can substitute the zero_page and potentially
  // consulting a page_source.
  vm_page_t* p = nullptr;
  if (page_or_mark && page_or_mark->IsPageOrRef()) {
    if (page_or_mark->IsReference()) {
      AssertHeld(page_owner->lock_);
      zx_status_t status =
          page_owner->ReplaceReferenceWithPageLocked(page_or_mark, owner_offset, page_request);
      if (status != ZX_OK) {
        return status;
      }
    }
    p = page_or_mark->Page();
  } else {
    // We need to get a real page as our initial content. At this point we are either starting from
    // the zero page, or something supplied from a page source. The page source only fills in if we
    // have a true absence of content.
    //
    // We treat a page source that always supplies zeroes (does not preserve page content) as an
    // absence of content (given the lack of a page), but we can only use the zero page if we're not
    // writing, since we can't (or in case of not providing specific physical pages, shouldn't) let
    // an arbitrary physical page get added below - we need to only add the specific physical pages
    // supplied by the source.
    //
    // In the case of a (hypothetical) page source that's both always providing zeroes and not
    // suppying specific physical pages, we intentionally ask the page source to supply the pages
    // here since otherwise there's no point in having such a page source.  We have no such page
    // sources currently.
    //
    // Contiguous VMOs don't use markers and always have a page source, so the first two conditions
    // won't be true for a contiguous VMO.
    AssertHeld(page_owner->lock_);
    if ((page_or_mark && page_or_mark->IsMarker()) || !page_owner->page_source_ ||
        (!writing && !page_owner->is_source_preserving_page_content())) {
      // We case use the zero page, since we have a marker, or no page source, or we're not adding
      // a page to the VmCowPages (due to !writing) and the page source always provides zeroes so
      // reading zeroes is consistent with what the page source would provide.
      p = vm_get_zero_page();
    } else {
      // We will attempt to get the page from the page source.

      AssertHeld(page_owner->lock_);
      // Before requesting the page source, check if we can implicitly supply a zero page. Pages in
      // the range [supply_zero_offset_, size_) can be supplied with zeros.
      if (owner_offset >= page_owner->supply_zero_offset_) {
        // The supply_zero_offset_ is only relevant for page sources preserving page content. For
        // other types of VMOs, the supply_zero_offset_ will be set to UINT64_MAX, so we can never
        // end up here.
        DEBUG_ASSERT(page_owner->is_source_preserving_page_content());
        DEBUG_ASSERT(IS_PAGE_ALIGNED(page_owner->supply_zero_offset_));
        DEBUG_ASSERT(page_owner->supply_zero_offset_ <= page_owner->size_);

        // Set p to the zero page and fall through. We will correctly fork the zero page if we're
        // writing to it.
        p = vm_get_zero_page();
      } else {
        // Otherwise request the page from the page source.
        uint64_t user_id = 0;
        if (page_owner->paged_ref_) {
          AssertHeld(page_owner->paged_ref_->lock_ref());
          user_id = page_owner->paged_ref_->user_id_locked();
        }
        VmoDebugInfo vmo_debug_info = {
            .vmo_ptr = reinterpret_cast<uintptr_t>(page_owner->paged_ref_), .vmo_id = user_id};
        zx_status_t status =
            page_owner->page_source_->GetPage(owner_offset, page_request->get(), vmo_debug_info);
        // Page sources will never synchronously return a page.
        DEBUG_ASSERT(status != ZX_OK);

        return status;
      }
    }
  }

  // If we made it this far we must have some valid vm_page in |p|. Although this may be the zero
  // page, the rest of this function is tolerant towards correctly forking it.
  DEBUG_ASSERT(p);
  // It's possible that we are going to fork the page, and the user isn't actually going to directly
  // use `p`, but creating the fork still uses `p` so we want to consider it accessed.
  AssertHeld(page_owner->lock_);
  page_owner->UpdateOnAccessLocked(p, pf_flags);

  if (!writing) {
    // If we're read-only faulting, return the page so they can map or read from it directly,
    // grabbing any additional pages if visible.
    out->add_page(p->paddr());
    if (visible_length > PAGE_SIZE) {
      collect_pages(page_owner, owner_offset + PAGE_SIZE, visible_length - PAGE_SIZE);
    }
    LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, p->paddr());
    return ZX_OK;
  }

  // From here we must allocate additional pages, which we may only do if acting on a software or
  // hardware fault.
  if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  // We assume we are writing from this point on. Otherwise, we should have returned above.
  DEBUG_ASSERT(writing);

  vm_page_t* res_page;
  if (!page_owner->is_hidden_locked() || p == vm_get_zero_page()) {
    // If the page source is preserving content (is a PagerProxy), and is configured to trap dirty
    // transitions, we first need to generate a DIRTY request *before* the zero page can be forked
    // and marked dirty. If dirty transitions are not trapped, we will fall through to allocate the
    // page and then mark it dirty below.
    //
    // Note that the check for ShouldTrapDirtyTransitions() is an optimization here.
    // PrepareForWriteLocked() would do the right thing depending on ShouldTrapDirtyTransitions(),
    // however we choose to avoid the extra work only to have it be a no-op if dirty transitions
    // should not be trapped.
    //
    // We need to generate a DIRTY request if the caller explicitly requested so with mark_dirty, or
    // if the offset lies beyond supply_zero_offset_. A page that lies beyond supply_zero_offset_
    // *cannot* be Clean. A gap beyond supply_zero_offset_ is conceptually already dirty (and zero),
    // so we're transitioning to a dirty actual page here, i.e. we cannot lose dirtiness when we
    // fork the zero page here.
    if (is_source_preserving_page_content() && page_source_->ShouldTrapDirtyTransitions() &&
        (mark_dirty == DirtyTrackingAction::DirtyAllPagesOnWrite ||
         offset >= supply_zero_offset_)) {
      // The only page we can be forking here is the zero page. A non-slice child VMO does not
      // support dirty page tracking.
      DEBUG_ASSERT(p == vm_get_zero_page());
      // This object directly owns the page.
      DEBUG_ASSERT(page_owner == this);

      // When generating the DIRTY request, try to extend the range beyond the immediate page, to
      // include other non-dirty pages and markers within the requested range. This is an
      // optimization aimed at reducing the number of distinct calls to LookupPagesLocked, and hence
      // the number of distinct DIRTY page requests generated for consecutive pages that need DIRTY
      // requests.
      uint64_t dirty_len = 0;
      zx_status_t status =
          PrepareForWriteLocked(offset, max_out_pages * PAGE_SIZE, page_request, &dirty_len);
      // The page source will never succeed synchronously.
      DEBUG_ASSERT(status != ZX_OK);
      // No pages will have been dirtied. The range starts with a marker, so we won't be able to
      // accumulate any committed dirty pages.
      DEBUG_ASSERT(dirty_len == 0);
      // No pages to return yet.
      out->num_pages = 0;
      return status;
    }

    // The general pmm_alloc_flags_ are not allowed to contain the BORROW option, and this is relied
    // upon below to assume the page allocated cannot be loaned.
    DEBUG_ASSERT(!(pmm_alloc_flags_ & PMM_ALLOC_FLAG_CAN_BORROW));

    // If the vmo isn't hidden, we can't move the page. If the page is the zero
    // page, there's no need to try to move the page. In either case, we need to
    // allocate a writable page for this vmo.
    DEBUG_ASSERT(page_request);
    zx_status_t alloc_status =
        AllocateCopyPage(pmm_alloc_flags_, p->paddr(), alloc_list, page_request, &res_page);
    if (unlikely(alloc_status != ZX_OK)) {
      return alloc_status;
    }
    VmPageOrMarker insert = VmPageOrMarker::Page(res_page);

    // We could be allocating a page to replace a zero page marker in a pager-backed VMO. If we were
    // asked to dirty the page, mark it Dirty, otherwise mark it Clean. AddPageLocked below will
    // then insert the page into the appropriate page queue.
    if (is_source_preserving_page_content()) {
      // The only page we can be forking here is the zero page. A non-slice child VMO does not
      // support dirty page tracking.
      DEBUG_ASSERT(p == vm_get_zero_page());
      // This object directly owns the page.
      DEBUG_ASSERT(page_owner == this);

      // The forked page was just allocated, and so cannot be a loaned page.
      DEBUG_ASSERT(!res_page->is_loaned());

      // Mark the forked page dirty or clean depending on the mark_dirty action requested. However,
      // if the page lies beyond supply_zero_offset_ it *cannot* be Clean. A gap beyond
      // supply_zero_offset_ is conceptually already dirty (and zero), so we're transitioning to a
      // dirty actual page here, i.e. we cannot lose dirtiness when we fork the zero page here.
      UpdateDirtyStateLocked(
          res_page, offset,
          mark_dirty == DirtyTrackingAction::DirtyAllPagesOnWrite || offset >= supply_zero_offset_
              ? DirtyState::Dirty
              : DirtyState::Clean,
          /*is_pending_add=*/true);
    }

    zx_status_t status = AddPageLocked(&insert, offset, CanOverwriteContent::Zero, nullptr);
    if (status != ZX_OK) {
      // AddPageLocked failing for any other reason is a programming error.
      DEBUG_ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "status=%d\n", status);
      // We are freeing a page we just got from the PMM (or from the alloc_list), so we do not own
      // it yet.
      FreePageLocked(insert.ReleasePage(), /*freeing_owned_page=*/false);
      return status;
    }
    // Interpret a software fault as an explicit desire to have potential zero pages and don't
    // consider them for cleaning, this is an optimization.
    //
    // We explicitly must *not* place pages from a page_source_ that's using pager queues into the
    // zero scanning queue, as the pager queues are already using the backlink.
    //
    // We don't need to scan for zeroes if on finding zeroes we wouldn't be able to remove the page
    // anyway.
    if (p == vm_get_zero_page() && !is_source_preserving_page_content() &&
        can_decommit_zero_pages_locked() && !(pf_flags & VMM_PF_FLAG_SW_FAULT)) {
      pmm_page_queues()->MoveToAnonymousZeroFork(res_page);
    }

    // This is the only path where we can allocate a new page without being a clone (clones are
    // always cached). So we check here if we are not fully cached and if so perform a
    // clean/invalidate to flush our zeroes. After doing this we will not touch the page via the
    // physmap and so we can pretend there isn't an aliased mapping.
    // There are three potential states that may exist
    //  * VMO is cached, paged_ref_ might be null, we might have children -> no cache op needed
    //  * VMO is uncached, paged_ref_ is not null, we have no children -> cache op needed
    //  * VMO is uncached, paged_ref_ is null, we have no children -> cache op not needed /
    //                                                                state cannot happen
    // In the uncached case we know we have no children, since it is by definition not valid to
    // have copy-on-write children of uncached pages. The third case cannot happen, but even if it
    // could with no children and no paged_ref_ the pages cannot actually be referenced so any
    // cache operation is pointless.
    if (paged_ref_) {
      AssertHeld(paged_ref_->lock_ref());
      if (paged_ref_->GetMappingCachePolicyLocked() != ARCH_MMU_FLAG_CACHED) {
        arch_clean_invalidate_cache_range((vaddr_t)paddr_to_physmap(res_page->paddr()), PAGE_SIZE);
      }
    }
  } else {
    // We need a writable page; let ::CloneCowPageLocked handle inserting one.
    zx_status_t result = CloneCowPageLocked(offset, alloc_list, page_owner, p, owner_offset,
                                            page_request, &res_page);
    if (result != ZX_OK) {
      return result;
    }
    VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
    VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  }

  LTRACEF("faulted in page %p, pa %#" PRIxPTR "\n", res_page, res_page->paddr());

  out->add_page(res_page->paddr());

  // If we made it here, we committed a new page in this VMO.
  IncrementHierarchyGenerationCountLocked();

  return ZX_OK;
}

zx_status_t VmCowPages::CommitRangeLocked(uint64_t offset, uint64_t len, uint64_t* committed_len,
                                          LazyPageRequest* page_request) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);

    // PagedParentOfSliceLocked will walk all of the way up the VMO hierarchy
    // until it hits a non-slice VMO.  This guarantees that we should only ever
    // recurse once instead of an unbound number of times.  DEBUG_ASSERT this so
    // that we don't actually end up with unbound recursion just in case the
    // property changes.
    DEBUG_ASSERT(!parent->is_slice_locked());

    return parent->CommitRangeLocked(offset + parent_offset, len, committed_len, page_request);
  }

  fbl::RefPtr<PageSource> root_source = GetRootPageSourceLocked();

  // If this vmo has a direct page source, then the source will provide the backing memory. For
  // children that eventually depend on a page source, we skip preallocating memory to avoid
  // potentially overallocating pages if something else touches the vmo while we're blocked on the
  // request. Otherwise we optimize things by preallocating all the pages.
  list_node page_list;
  list_initialize(&page_list);
  if (root_source == nullptr) {
    // make a pass through the list to find out how many pages we need to allocate
    size_t count = len / PAGE_SIZE;
    page_list_.ForEveryPageInRange(
        [&count](const auto* p, auto off) {
          if (p->IsPage()) {
            count--;
          }
          return ZX_ERR_NEXT;
        },
        offset, offset + len);

    if (count == 0) {
      *committed_len = len;
      return ZX_OK;
    }

    zx_status_t status = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
    // Ignore ZX_ERR_SHOULD_WAIT since the loop below will fall back to a page by page allocation,
    // allowing us to wait for single pages should we need to.
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      return status;
    }
  }

  auto list_cleanup = fit::defer([&page_list, this]() {
    if (!list_is_empty(&page_list)) {
      AssertHeld(lock_);
      // We are freeing pages we got from the PMM and did not end up using, so we do not own them.
      FreePagesLocked(&page_list, /*freeing_owned_pages=*/false);
    }
  });

  const uint64_t start_offset = offset;
  const uint64_t end = offset + len;
  bool have_page_request = false;
  LookupInfo lookup_info;
  while (offset < end) {
    // Don't commit if we already have this page
    const VmPageOrMarker* p = page_list_.Lookup(offset);
    if (!p || !p->IsPage()) {
      const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
      // A commit does not imply that pages are being dirtied, they are just being populated.
      zx_status_t res = LookupPagesLocked(offset, flags, DirtyTrackingAction::None, 1, &page_list,
                                          page_request, &lookup_info);
      if (unlikely(res == ZX_ERR_SHOULD_WAIT)) {
        if (page_request->get()->BatchAccepting()) {
          // In batch mode, will need to finalize the request later.
          if (!have_page_request) {
            // Stash how much we have committed right now, as we are going to have to reprocess this
            // range so we do not want to claim it was committed.
            *committed_len = offset - start_offset;
            have_page_request = true;
          }
        } else {
          // We can end up here in two cases:
          // 1. We were in batch mode but had to terminate the batch early.
          // 2. We hit the first missing page and we were not in batch mode.
          //
          // If we do have a page request, that means the batch was terminated early by
          // pre-populated pages (case 1). Return immediately.
          //
          // Do not update the |committed_len| for case 1 as we are returning on encountering
          // pre-populated pages while processing a batch. When that happens, we will terminate the
          // batch we were processing and send out a page request for the contiguous range we've
          // accumulated in the batch so far. And we will need to come back into this function again
          // to reprocess the range the page request spanned, so we cannot claim any pages have been
          // committed yet.
          if (!have_page_request) {
            // Not running in batch mode, and this is the first missing page (case 2). Update the
            // committed length we have so far and return.
            *committed_len = offset - start_offset;
          }
          return ZX_ERR_SHOULD_WAIT;
        }
      } else if (unlikely(res != ZX_OK)) {
        VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
        VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
        return res;
      }
    }

    offset += PAGE_SIZE;
  }

  if (have_page_request) {
    // commited_len was set when have_page_request was set so can just return.
    return page_request->get()->FinalizeRequest();
  }

  // Processed the full range successfully
  *committed_len = len;
  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return ZX_OK;
}

zx_status_t VmCowPages::PinRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);

    // PagedParentOfSliceLocked will walk all of the way up the VMO hierarchy
    // until it hits a non-slice VMO.  This guarantees that we should only ever
    // recurse once instead of an unbound number of times.  DEBUG_ASSERT this so
    // that we don't actually end up with unbound recursion just in case the
    // property changes.
    DEBUG_ASSERT(!parent->is_slice_locked());

    return parent->PinRangeLocked(offset + parent_offset, len);
  }

  ever_pinned_ = true;

  // Tracks our expected page offset when iterating to ensure all pages are present.
  uint64_t next_offset = offset;

  // Should any errors occur we need to unpin everything.
  auto pin_cleanup = fit::defer([this, offset, &next_offset]() {
    if (next_offset > offset) {
      AssertHeld(*lock());
      UnpinLocked(offset, next_offset - offset, /*allow_gaps=*/false);
    }
  });

  zx_status_t status = page_list_.ForEveryPageInRange(
      [this, &next_offset](const VmPageOrMarker* p, uint64_t page_offset) {
        AssertHeld(lock_);
        if (page_offset != next_offset || !p->IsPage()) {
          return ZX_ERR_BAD_STATE;
        }
        vm_page_t* page = p->Page();
        DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
        DEBUG_ASSERT(!page->is_loaned());

        if (page->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
          return ZX_ERR_UNAVAILABLE;
        }

        page->object.pin_count++;
        if (page->object.pin_count == 1) {
          MoveToPinnedLocked(page, page_offset);
        }

        // Pinning every page in the largest vmo possible as many times as possible can't overflow
        static_assert(VmPageList::MAX_SIZE / PAGE_SIZE < UINT64_MAX / VM_PAGE_OBJECT_MAX_PIN_COUNT);
        next_offset += PAGE_SIZE;
        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  const uint64_t actual = (next_offset - offset) / PAGE_SIZE;
  // Count whatever pages we pinned, in the failure scenario this will get decremented on the unpin.
  pinned_page_count_ += actual;

  if (status == ZX_OK) {
    // If the missing pages were at the end of the range (or the range was empty) then our iteration
    // will have just returned ZX_OK. Perform one final check that we actually pinned the number of
    // pages we expected to.
    const uint64_t expected = len / PAGE_SIZE;
    if (actual != expected) {
      status = ZX_ERR_BAD_STATE;
    } else {
      pin_cleanup.cancel();
    }
  }
  return status;
}

zx_status_t VmCowPages::DecommitRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // Trim the size and perform our zero-length hot-path check before we recurse
  // up to our top-level ancestor.  Size bounding needs to take place relative
  // to the child the operation was originally targeted against.
  uint64_t new_len;
  if (!TrimRange(offset, len, size_, &new_len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // was in range, just zero length
  if (new_len == 0) {
    return ZX_OK;
  }

  // If this is a child slice of a VMO, then find our way up to our root
  // ancestor (taking our offset into account as we do), and then recurse,
  // running the operation against our ancestor.  Note that
  // PagedParentOfSliceLocked will iteratively walk all the way up to our
  // non-slice ancestor, not just our immediate parent, so we can guaranteed
  // bounded recursion.
  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    DEBUG_ASSERT(!parent->is_slice_locked());  // assert bounded recursion.
    return parent->DecommitRangeLocked(offset + parent_offset, new_len);
  }

  // Currently, we can't decommit if the absence of a page doesn't imply zeroes.
  if (parent_ || is_source_preserving_page_content()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // VmObjectPaged::DecommitRange() rejects is_contiguous() VMOs (for now).
  DEBUG_ASSERT(can_decommit());

  // Demand offset and length be correctly aligned to not give surprising user semantics.
  if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  list_node_t freed_list;
  list_initialize(&freed_list);
  zx_status_t status = UnmapAndRemovePagesLocked(offset, new_len, &freed_list);
  if (status != ZX_OK) {
    return status;
  }

  FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);

  return status;
}

zx_status_t VmCowPages::UnmapAndRemovePagesLocked(uint64_t offset, uint64_t len,
                                                  list_node_t* freed_list,
                                                  uint64_t* pages_freed_out) {
  canary_.Assert();

  if (AnyPagesPinnedLocked(offset, len)) {
    return ZX_ERR_BAD_STATE;
  }

  LTRACEF("start offset %#" PRIx64 ", end %#" PRIx64 "\n", offset, offset + len);

  // We've already trimmed the range in DecommitRangeLocked().
  DEBUG_ASSERT(InRange(offset, len, size_));

  // Verify page alignment.
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len) || (offset + len == size_));

  // DecommitRangeLocked() will call this function only on a VMO with no parent. The only clone
  // types that support OP_DECOMMIT are slices, for which we will recurse up to the root.
  DEBUG_ASSERT(!parent_);

  // unmap all of the pages in this range on all the mapping regions
  RangeChangeUpdateLocked(offset, len, RangeChangeOp::Unmap);

  __UNINITIALIZED BatchPQRemove page_remover(freed_list);

  page_list_.RemovePages(page_remover.RemovePagesCallback(), offset, offset + len);
  page_remover.Flush();

  if (pages_freed_out) {
    *pages_freed_out = page_remover.freed_count();
  }

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return ZX_OK;
}

bool VmCowPages::PageWouldReadZeroLocked(uint64_t page_offset) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_offset));
  DEBUG_ASSERT(page_offset < size_);
  const VmPageOrMarker* slot = page_list_.Lookup(page_offset);
  if (slot && slot->IsMarker()) {
    // This is already considered zero as there's a marker.
    return true;
  }
  if (is_source_preserving_page_content() && page_offset >= supply_zero_offset_) {
    // Uncommitted pages beyond supply_zero_offset_ are supplied as zeros by the kernel.
    if (!slot || slot->IsEmpty()) {
      return true;
    }
  }
  // If we don't have a page or reference here we need to check our parent.
  if (!slot || !slot->IsPageOrRef()) {
    VmCowPages* page_owner;
    uint64_t owner_offset;
    if (!FindInitialPageContentLocked(page_offset, &page_owner, &owner_offset, nullptr)) {
      // Parent doesn't have a page either, so would also read as zero, assuming no page source.
      return GetRootPageSourceLocked() == nullptr;
    }
  }
  // Content either locally or in our parent, assume it is non-zero and return false.
  return false;
}

zx_status_t VmCowPages::ZeroPagesLocked(uint64_t page_start_base, uint64_t page_end_base,
                                        LazyPageRequest* page_request, uint64_t* zeroed_len_out) {
  canary_.Assert();

  DEBUG_ASSERT(page_start_base <= page_end_base);
  DEBUG_ASSERT(page_end_base <= size_);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_start_base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_end_base));
  ASSERT(zeroed_len_out);

  // Forward any operations on slices up to the original non slice parent.
  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->ZeroPagesLocked(page_start_base + parent_offset, page_end_base + parent_offset,
                                   page_request, zeroed_len_out);
  }

  // This function tries to zero pages as optimally as possible for most cases, so we attempt
  // increasingly expensive actions only if certain preconditions do not allow us to perform the
  // cheaper action. Broadly speaking, the sequence of actions that are attempted are as follows.
  //  1) Try to decommit the entire range at once if the VMO allows it.
  //  2) Otherwise, try to decommit each page if the VMO allows it and doing so doesn't expose
  //  content in the parent (if any) that shouldn't be visible.
  //  3) Otherwise, if this is a child VMO and there is no committed page yet, allocate a zero page.
  //  4) Otherwise, look up the page, faulting it in if necessary, and zero the page. If the page
  //  source needs to supply or dirty track the page, a page request is initialized and we return
  //  early with ZX_ERR_SHOULD_WAIT. The caller is expected to wait on the page request, and then
  //  retry. On the retry, we should be able to look up the page successfully and zero it.

  // First try and do the more efficient decommit. We prefer/ decommit as it performs work in the
  // order of the number of committed pages, instead of work in the order of size of the range. An
  // error from DecommitRangeLocked indicates that the VMO is not of a form that decommit can safely
  // be performed without exposing data that we shouldn't between children and parents, but no
  // actual state will have been changed. Should decommit succeed we are done, otherwise we will
  // have to handle each offset individually.
  //
  // Zeroing doesn't decommit pages of contiguous VMOs.
  if (can_decommit_zero_pages_locked()) {
    zx_status_t status = DecommitRangeLocked(page_start_base, page_end_base - page_start_base);
    if (status == ZX_OK) {
      *zeroed_len_out = page_end_base - page_start_base;
      return ZX_OK;
    }

    // Unmap any page that is touched by this range in any of our, or our childrens, mapping
    // regions. We do this on the assumption we are going to be able to free pages either completely
    // or by turning them into markers and it's more efficient to unmap once in bulk here.
    RangeChangeUpdateLocked(page_start_base, page_end_base - page_start_base, RangeChangeOp::Unmap);
  }

  // We stack-own loaned pages from when they're removed until they're freed.
  __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

  // Pages removed from this object are put into freed_list, while pages removed from any ancestor
  // are put into ancestor_freed_list. This is so that freeing of both the lists can be handled
  // correctly, by passing the correct value for freeing_owned_pages in the call to
  // FreePagesLocked().
  list_node_t freed_list;
  list_initialize(&freed_list);
  list_node_t ancestor_freed_list;
  list_initialize(&ancestor_freed_list);

  // See also free_any_pages below, which intentionally frees incrementally.
  auto auto_free = fit::defer([this, &freed_list, &ancestor_freed_list]() {
    AssertHeld(lock_);
    if (!list_is_empty(&freed_list)) {
      FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);
    }
    if (!list_is_empty(&ancestor_freed_list)) {
      FreePagesLocked(&ancestor_freed_list, /*freeing_owned_pages=*/false);
    }
  });

  // Ideally we just collect up pages and hand them over to the pmm all at the end, but if we need
  // to allocate any pages then we would like to ensure that we do not cause total memory to peak
  // higher due to squirreling these pages away.
  auto free_any_pages = [this, &freed_list, &ancestor_freed_list] {
    AssertHeld(lock_);
    if (!list_is_empty(&freed_list)) {
      FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);
    }
    if (!list_is_empty(&ancestor_freed_list)) {
      FreePagesLocked(&ancestor_freed_list, /*freeing_owned_pages=*/false);
    }
  };

  // Give us easier names for our range.
  const uint64_t start = page_start_base;
  const uint64_t end = page_end_base;

  // If we're zeroing at the end of our parent range we can update to reflect this similar to a
  // resize. This does not work if we are a slice, but we checked for that earlier. Whilst this does
  // not actually zero the range in question, it makes future zeroing of the range far more
  // efficient, which is why we do it first.
  if (start < parent_limit_ && end >= parent_limit_) {
    bool hidden_parent = false;
    if (parent_) {
      AssertHeld(parent_->lock_ref());
      hidden_parent = parent_->is_hidden_locked();
    }
    if (hidden_parent) {
      // Release any COW pages that are no longer necessary. This will also
      // update the parent limit.
      __UNINITIALIZED BatchPQRemove page_remover(&ancestor_freed_list);
      ReleaseCowParentPagesLocked(start, parent_limit_, &page_remover);
      page_remover.Flush();
    } else {
      parent_limit_ = start;
    }
  }

  // If the source preserves page content, empty slots beyond supply_zero_offset_ are implicitly
  // dirty and zero. Therefore, if supply_zero_offset_ falls in the specified range, we can simply
  // update supply_zero_offset_ to start, indicating that the range from start is now all dirty and
  // zero. Removing pages and markers beyond supply_zero_offset_ is going to be handled in the main
  // page traversal loop. The only exception here is if there are any pinned pages which we will not
  // be able to remove, so simply skip this optimization in that case and fall back to the general
  // case.
  if (is_source_preserving_page_content() &&
      (start < supply_zero_offset_ && supply_zero_offset_ <= end) &&
      !AnyPagesPinnedLocked(start, supply_zero_offset_ - start)) {
    // Resolve any read requests that might exist in the range [start, supply_zero_offset_), since
    // this range is now going to be supplied as zeroes by the kernel; the user pager cannot supply
    // pages in this range anymore.
    InvalidateReadRequestsLocked(start, supply_zero_offset_ - start);

    UpdateSupplyZeroOffsetLocked(start);
  }

  // If the VMO is directly backed by a page source that preserves content, it should be the root
  // VMO of the hierarchy.
  DEBUG_ASSERT(!is_source_preserving_page_content() || !parent_);

  // Helper lambda to determine if this VMO can see parent contents at offset, or if a length is
  // specified as well in the range [offset, offset + length).
  auto can_see_parent = [this](uint64_t offset, uint64_t length = PAGE_SIZE) TA_REQ(lock_) {
    if (!parent_) {
      return false;
    }
    return offset < parent_limit_ && offset + length <= parent_limit_;
  };

  // This is a lambda as it only makes sense to talk about parent mutability when we have a parent
  // for the offset being considered.
  auto parent_immutable = [can_see_parent, this](uint64_t offset) TA_REQ(lock_) {
    DEBUG_ASSERT(can_see_parent(offset));
    AssertHeld(parent_->lock_ref());
    return parent_->is_hidden_locked();
  };

  // Finding the initial page content is expensive, but we only need to call it under certain
  // circumstances scattered in the code below. The lambda get_initial_page_content() will lazily
  // fetch and cache the details. This avoids us calling it when we don't need to, or calling it
  // more than once.
  struct InitialPageContent {
    bool inited = false;
    VmCowPages* page_owner;
    uint64_t owner_offset;
    uint64_t cached_offset;
    VmPageOrMarkerRef page_or_marker;
  } initial_content_;
  auto get_initial_page_content = [&initial_content_, can_see_parent, this](uint64_t offset)
                                      TA_REQ(lock_) -> const InitialPageContent& {
    // If there is no cached page content or if we're looking up a different offset from the cached
    // one, perform the lookup.
    if (!initial_content_.inited || offset != initial_content_.cached_offset) {
      DEBUG_ASSERT(can_see_parent(offset));
      VmPageOrMarkerRef page_or_marker = FindInitialPageContentLocked(
          offset, &initial_content_.page_owner, &initial_content_.owner_offset, nullptr);
      // We only care about the parent having a 'true' vm_page for content. If the parent has a
      // marker then it's as if the parent has no content since that's a zero page anyway, which is
      // what we are trying to achieve.
      initial_content_.page_or_marker = page_or_marker;
      initial_content_.inited = true;
      initial_content_.cached_offset = offset;
    }
    DEBUG_ASSERT(offset == initial_content_.cached_offset);
    return initial_content_;
  };

  // Helper lambda to determine if parent has content at the specified offset.
  auto parent_has_content = [get_initial_page_content](uint64_t offset) TA_REQ(lock_) {
    const VmPageOrMarkerRef& page_or_marker = get_initial_page_content(offset).page_or_marker;
    return page_or_marker && page_or_marker->IsPageOrRef();
  };

  // In the ideal case we can zero by making there be an Empty slot in our page list. This is true
  // when we're not specifically avoiding decommit on zero and there is nothing pinned.
  // Additionally, if the page source is preserving content, an empty slot at this offset should
  // imply zero, and this is only true for offsets starting at supply_zero_offset_. For offsets
  // preceding supply_zero_offset_ an empty slot signifies absent content that has not yet been
  // supplied by the page source.
  //
  // Note that this lambda is only checking for pre-conditions in *this* VMO which allow us to
  // represent zeros with an empty slot. We will combine this check with additional checks for
  // contents visible through the parent, if applicable.
  auto can_decommit_slot = [this](const VmPageOrMarker* slot, uint64_t offset) TA_REQ(lock_) {
    if (!can_decommit_zero_pages_locked() ||
        (slot && slot->IsPage() && slot->Page()->object.pin_count > 0)) {
      return false;
    }
    // Offsets less than supply_zero_offset_ cannot be decommitted.
    return !is_source_preserving_page_content() || offset >= supply_zero_offset_;
  };

  // Like can_decommit_slot but for a range.
  auto can_decommit_slots_in_range = [this](uint64_t offset, uint64_t length) TA_REQ(lock_) {
    if (!can_decommit_zero_pages_locked() || AnyPagesPinnedLocked(offset, length)) {
      return false;
    }
    // Offsets less than supply_zero_offset_ cannot be decommitted.
    return !is_source_preserving_page_content() || offset >= supply_zero_offset_;
  };

  // Helper lambda to zero the slot at offset either by inserting a marker or by zeroing the actual
  // page as applicable. The return codes match those expected for VmPageList traversal.
  auto zero_slot = [&](VmPageOrMarker* slot, uint64_t offset) TA_REQ(lock_) {
    // Ideally we will use a marker, but we can only do this if we can point to a committed page
    // to justify the allocation of the marker (i.e. we cannot allocate infinite markers with no
    // committed pages). A committed page in this case exists if the parent has any content.
    // Otherwise, we'll need to zero an actual page.
    if (!can_decommit_slot(slot, offset) || !parent_has_content(offset)) {
      // We might allocate a new page below. Free any pages we've accumulated first.
      free_any_pages();

      // If we're here because of !parent_has_content() and slot doesn't have a page, we can simply
      // allocate a zero page to replace the empty slot. Otherwise, we'll have to look up the page
      // and zero it.
      //
      // We could technically fall through to LookupPagesLocked even for an empty slot and let
      // LookupPagesLocked allocate a new page and zero it, but we want to avoid having to
      // redundantly zero a newly forked zero page after LookupPagesLocked.
      if (!slot && can_see_parent(offset) && !parent_has_content(offset)) {
        // We could only have ended up here if the parent was mutable, otherwise we should have been
        // able to treat an empty slot as zero (decommit a committed page) and return early above.
        DEBUG_ASSERT(!parent_immutable(offset));
        // We will try to insert a new zero page below. Note that at this point we know that this is
        // not a contiguous VMO (which cannot have arbitrary zero pages inserted into it). We
        // checked for can_see_parent just now and contiguous VMOs do not support (non-slice)
        // clones. Besides, if the slot was empty we should have moved on when we found the gap in
        // the page list traversal as the contiguous page source zeroes supplied pages by default.
        DEBUG_ASSERT(!debug_is_contiguous());

        // Allocate a new page, it will be zeroed in the process.
        vm_page_t* p;
        // Do not pass our freed_list here as this takes an |alloc_list| list to allocate from.
        zx_status_t status =
            AllocateCopyPage(pmm_alloc_flags_, vm_get_zero_page_paddr(), nullptr, page_request, &p);
        if (status != ZX_OK) {
          return status;
        }
        VmPageOrMarker new_page = VmPageOrMarker::Page(p);
        status = AddPageLocked(&new_page, offset, CanOverwriteContent::Zero, nullptr,
                               /*do_range_update=*/false);
        // Absent bugs, AddPageLocked() can only return ZX_ERR_NO_MEMORY.
        if (status == ZX_ERR_NO_MEMORY) {
          return status;
        }
        DEBUG_ASSERT(status == ZX_OK);
        return ZX_ERR_NEXT;
      }

      // Lookup the page which will potentially fault it in via the page source. Zeroing is
      // equivalent to a VMO write with zeros, so simulate a write fault.
      __UNINITIALIZED LookupInfo lookup_page;
      zx_status_t status = LookupPagesLocked(offset, VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE,
                                             VmObject::DirtyTrackingAction::DirtyAllPagesOnWrite, 1,
                                             nullptr, page_request, &lookup_page);
      if (status != ZX_OK) {
        return status;
      }

      // Zero the page we looked up.
      DEBUG_ASSERT(lookup_page.num_pages == 1);
      ZeroPage(lookup_page.paddrs[0]);
      return ZX_ERR_NEXT;
    }

    DEBUG_ASSERT(parent_ && parent_has_content(offset));
    DEBUG_ASSERT(!debug_is_contiguous());

    // We are able to insert a marker, but if our page content is from a hidden owner we need to
    // perform slightly more complex cow forking.
    const InitialPageContent& content = get_initial_page_content(offset);
    AssertHeld(content.page_owner->lock_ref());
    if (!slot && content.page_owner->is_hidden_locked()) {
      free_any_pages();
      // TODO(fxbug.dev/60238): This could be more optimal since unlike a regular cow clone, we are
      // not going to actually need to read the target page we are cloning, and hence it does not
      // actually need to get converted.
      if (content.page_or_marker->IsReference()) {
        zx_status_t result = content.page_owner->ReplaceReferenceWithPageLocked(
            content.page_or_marker, content.owner_offset, page_request);
        if (result != ZX_OK) {
          return result;
        }
      }
      zx_status_t result = CloneCowPageAsZeroLocked(
          offset, &ancestor_freed_list, content.page_owner, content.page_or_marker->Page(),
          content.owner_offset, page_request);
      if (result != ZX_OK) {
        return result;
      }
      return ZX_ERR_NEXT;
    }

    // Remove any page that could be hanging around in the slot and replace it with a marker.
    VmPageOrMarker new_marker = VmPageOrMarker::Marker();
    VmPageOrMarker released_page;
    zx_status_t status = AddPageLocked(&new_marker, offset, CanOverwriteContent::NonZero,
                                       &released_page, /*do_range_update=*/false);
    // Absent bugs, AddPageLocked() can only return ZX_ERR_NO_MEMORY.
    if (status == ZX_ERR_NO_MEMORY) {
      return status;
    }
    DEBUG_ASSERT(status == ZX_OK);
    // Free the old page.
    if (released_page.IsPage()) {
      vm_page_t* page = released_page.ReleasePage();
      DEBUG_ASSERT(page->object.pin_count == 0);
      pmm_page_queues()->Remove(page);
      DEBUG_ASSERT(!list_in_list(&page->queue_node));
      list_add_tail(&freed_list, &page->queue_node);
    } else if (released_page.IsReference()) {
      FreeReference(released_page.ReleaseReference());
    }
    return ZX_ERR_NEXT;
  };

  *zeroed_len_out = 0;
  // Main page list traversal loop to remove any existing pages / markers, zero existing pages, and
  // also insert any new markers / zero pages in gaps as applicable. We use the VmPageList traversal
  // helper here instead of iterating over each offset in the range so we can efficiently skip over
  // gaps if possible.
  zx_status_t status = page_list_.RemovePagesAndIterateGaps(
      [&](VmPageOrMarker* slot, uint64_t offset) {
        AssertHeld(lock_);

        // Contiguous VMOs cannot have markers.
        DEBUG_ASSERT(!direct_source_supplies_zero_pages() || !slot->IsMarker());

        // First see if we can simply get done with an empty slot in the page list. This VMO should
        // allow decommitting a page at this offset when zeroing. Additionally, one of the following
        // conditions should hold w.r.t. to the parent:
        //  * This offset does not relate to our parent, or we don't have a parent.
        //  * This offset does relate to our parent, but our parent is immutable and is currently
        //  zero at this offset.
        if (can_decommit_slot(slot, offset) &&
            (!can_see_parent(offset) ||
             (parent_immutable(offset) && !parent_has_content(offset)))) {
          if (slot->IsPage()) {
            vm_page_t* page = slot->ReleasePage();
            pmm_page_queues()->Remove(page);
            DEBUG_ASSERT(!list_in_list(&page->queue_node));
            list_add_tail(&freed_list, &page->queue_node);
          } else if (slot->IsReference()) {
            FreeReference(slot->ReleaseReference());
          } else {
            // If this is a marker, simply make the slot empty.
            *slot = VmPageOrMarker::Empty();
          }
          // We successfully zeroed this offset. Move on to the next offset.
          *zeroed_len_out += PAGE_SIZE;
          return ZX_ERR_NEXT;
        }

        // If there's already a marker then we can avoid any second guessing and leave the marker
        // alone.
        if (slot->IsMarker()) {
          *zeroed_len_out += PAGE_SIZE;
          return ZX_ERR_NEXT;
        }

        // The only time we would reach here and *not* have a parent is if we could not decommit a
        // page at this offset when zeroing.
        DEBUG_ASSERT(!can_decommit_slot(slot, offset) || parent_);

        // Now we know that we need to do something active to make this zero, either through a
        // marker or a page.
        zx_status_t status = zero_slot(slot, offset);
        if (status == ZX_ERR_NEXT) {
          // If we were able to successfully zero this slot, move on to the next offset.
          *zeroed_len_out += PAGE_SIZE;
        }
        return status;
      },
      [&](uint64_t gap_start, uint64_t gap_end) {
        AssertHeld(lock_);
        if (direct_source_supplies_zero_pages()) {
          // Already logically zero - don't commit pages to back the zeroes if they're not already
          // committed.  This is important for contiguous VMOs, as we don't use markers for
          // contiguous VMOs, and allocating a page below to hold zeroes would not be asking the
          // page_source_ for the proper physical page. This prevents allocating an arbitrary
          // physical page to back the zeroes.
          *zeroed_len_out += (gap_end - gap_start);
          return ZX_ERR_NEXT;
        }

        // If empty slots imply zeroes, and the gap does not see parent contents, we already have
        // zeroes.
        if (can_decommit_slots_in_range(gap_start, gap_end - gap_start) &&
            !can_see_parent(gap_start, gap_end - gap_start)) {
          *zeroed_len_out += (gap_end - gap_start);
          return ZX_ERR_NEXT;
        }

        // Otherwise fall back to examining each offset in the gap to determine the action to
        // perform.
        for (uint64_t offset = gap_start; offset < gap_end;
             offset += PAGE_SIZE, *zeroed_len_out += PAGE_SIZE) {
          // First see if we can simply get done with an empty slot in the page list. This VMO
          // should allow decommitting a page at this offset when zeroing. Additionally, one of the
          // following conditions should hold w.r.t. to the parent:
          //  * This offset does not relate to our parent, or we don't have a parent.
          //  * This offset does relate to our parent, but our parent is immutable and is currently
          //  zero at this offset.
          if (can_decommit_slot(nullptr, offset) &&
              (!can_see_parent(offset) ||
               (parent_immutable(offset) && !parent_has_content(offset)))) {
            continue;
          }

          // The only time we would reach here and *not* have a parent is if we could not decommit a
          // page at this offset when zeroing.
          DEBUG_ASSERT(!can_decommit_slot(nullptr, offset) || parent_);

          // Now we know that we need to do something active to make this zero, either through a
          // marker or a page.
          zx_status_t status = zero_slot(nullptr, offset);
          if (status != ZX_ERR_NEXT) {
            return status;
          }
        }

        return ZX_ERR_NEXT;
      },
      start, end);

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return status;
}

void VmCowPages::MoveToPinnedLocked(vm_page_t* page, uint64_t offset) {
  pmm_page_queues()->MoveToWired(page);
}

void VmCowPages::MoveToNotPinnedLocked(vm_page_t* page, uint64_t offset) {
  if (is_source_preserving_page_content()) {
    DEBUG_ASSERT(is_page_dirty_tracked(page));
    // We can only move Clean pages to the pager backed queues as they track age information for
    // eviction; only Clean pages can be evicted. Pages in AwaitingClean and Dirty are protected
    // from eviction in the Dirty queue.
    if (is_page_clean(page)) {
      pmm_page_queues()->MoveToPagerBacked(page);
    } else {
      DEBUG_ASSERT(!page->is_loaned());
      pmm_page_queues()->MoveToPagerBackedDirty(page);
    }
  } else {
    // Place pages from contiguous VMOs in the wired queue, as they are notionally pinned until the
    // owner explicitly releases them.
    if (can_decommit_zero_pages_locked()) {
      pmm_page_queues()->MoveToAnonymous(page);
    } else {
      pmm_page_queues()->MoveToWired(page);
    }
  }
}

void VmCowPages::SetNotPinnedLocked(vm_page_t* page, uint64_t offset) {
  if (is_source_preserving_page_content()) {
    DEBUG_ASSERT(is_page_dirty_tracked(page));
    // We can only move Clean pages to the pager backed queues as they track age information for
    // eviction; only Clean pages can be evicted. Pages in AwaitingClean and Dirty are protected
    // from eviction in the Dirty queue.
    if (is_page_clean(page)) {
      pmm_page_queues()->SetPagerBacked(page, this, offset);
    } else {
      DEBUG_ASSERT(!page->is_loaned());
      pmm_page_queues()->SetPagerBackedDirty(page, this, offset);
    }
  } else {
    // Place pages from contiguous VMOs in the wired queue, as they are notionally pinned until the
    // owner explicitly releases them.
    if (can_decommit_zero_pages_locked()) {
      pmm_page_queues()->SetAnonymous(page, this, offset);
    } else {
      pmm_page_queues()->SetWired(page, this, offset);
    }
  }
}

void VmCowPages::PromoteRangeForReclamationLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // Hints only apply to pager backed VMOs.
  if (!can_root_source_evict_locked()) {
    return;
  }

  // Walk up the tree to get to the root parent. A raw pointer is fine as we're holding the lock and
  // won't drop it in this function.
  // We need the root to check if the pages are owned by the root below. Hints only apply to pages
  // in the root that are visible to this child, not to pages the child might have forked.
  const VmCowPages* const root = GetRootLocked();

  uint64_t start_offset = ROUNDDOWN(offset, PAGE_SIZE);
  uint64_t end_offset = ROUNDUP(offset + len, PAGE_SIZE);

  __UNINITIALIZED LookupInfo lookup;
  while (start_offset < end_offset) {
    // Don't pass in any fault flags. We only want to lookup an existing page. Note that we do want
    // to look up the page in the child, instead of just forwarding the entire range lookup to the
    // parent, because we do NOT want to hint pages in the parent that have already been forked in
    // the child. That is, we need to first lookup the page and then check for ownership.
    zx_status_t status =
        LookupPagesLocked(start_offset, 0, DirtyTrackingAction::None, 1, nullptr, nullptr, &lookup);
    // Successfully found an existing page.
    if (status == ZX_OK) {
      DEBUG_ASSERT(lookup.num_pages == 1);
      vm_page_t* page = paddr_to_vm_page(lookup.paddrs[0]);
      // Check to see if the page is owned by the root VMO. Hints only apply to the root.
      // Don't move a pinned page or a dirty page to the DontNeed queue.
      // Note that this does not unset the always_need bit if it has been previously set. The
      // always_need hint is sticky.
      if (page->object.get_object() == root && page->object.pin_count == 0 && is_page_clean(page)) {
        pmm_page_queues()->MoveToPagerBackedDontNeed(page);
      }
    }
    // Can't really do anything in case an error is encountered while looking up the page. Simply
    // ignore it and move on to the next page. Hints are best effort anyway.
    start_offset += PAGE_SIZE;
  }
}

void VmCowPages::ProtectRangeFromReclamationLocked(uint64_t offset, uint64_t len,
                                                   Guard<CriticalMutex>* guard) {
  canary_.Assert();

  // Hints only apply to pager backed VMOs.
  if (!can_root_source_evict_locked()) {
    return;
  }

  uint64_t cur_offset = ROUNDDOWN(offset, PAGE_SIZE);
  uint64_t end_offset = ROUNDUP(offset + len, PAGE_SIZE);

  __UNINITIALIZED LookupInfo lookup;
  __UNINITIALIZED LazyPageRequest page_request;
  for (; cur_offset < end_offset; cur_offset += PAGE_SIZE) {
    // Simulate a read fault. We simply want to lookup the page in the parent (if visible from the
    // child), without forking the page in the child. Note that we do want to look up the page in
    // the child, instead of just forwarding the entire range lookup to the parent, because we do
    // NOT want to hint pages in the parent that have already been forked in the child. That is, we
    // need to first lookup the page and then check for ownership.
    zx_status_t status =
        LookupPagesLocked(cur_offset, VMM_PF_FLAG_SW_FAULT, DirtyTrackingAction::None, 1, nullptr,
                          &page_request, &lookup);

    if (status == ZX_OK) {
      // If we reached here, we successfully found a page at the current offset.
      DEBUG_ASSERT(lookup.num_pages == 1);
      vm_page_t* page = paddr_to_vm_page(lookup.paddrs[0]);

      // The root might have gone away when the lock was dropped while waiting above. Compute the
      // root again and check if we still have a page source backing it before applying the hint.
      if (!can_root_source_evict_locked()) {
        // Hinting is not applicable anymore. No more pages to hint.
        return;
      }

      // Check to see if the page is owned by the root VMO. Hints only apply to the root.
      VmCowPages* owner = reinterpret_cast<VmCowPages*>(page->object.get_object());
      if (owner != GetRootLocked()) {
        // Hinting is not applicable to this page, but it might apply to following ones.
        continue;
      }

      // If the page is loaned, replace it with a non-loaned page. Loaned pages are reclaimed by
      // eviction, and hinted pages should not be evicted.
      if (page->is_loaned()) {
        DEBUG_ASSERT(is_page_clean(page));
        AssertHeld(owner->lock_);
        status = owner->ReplacePageLocked(page, page->object.get_page_offset(),
                                          /*with_loaned=*/false, &page, &page_request);
        // Let the status fall through below to have success, waiting and errors handled.
      }

      if (status == ZX_OK) {
        DEBUG_ASSERT(!page->is_loaned());
        page->object.always_need = 1;
        // Nothing more to do beyond marking the page always_need true. The lookup must have already
        // marked the page accessed, moving it to the head of the first page queue.
        continue;
      }
    }

    // We need to wait for the page to be faulted in or available for allocation.
    // We will drop the lock as we wait.
    if (status == ZX_ERR_SHOULD_WAIT) {
      guard->CallUnlocked([&status, &page_request]() { status = page_request->Wait(); });

      // The size might have changed since we dropped the lock. Adjust the range if required.
      if (cur_offset >= size_locked()) {
        // No more pages to hint.
        return;
      }
      // Shrink the range if required. Proceed with hinting on the remaining pages in the range;
      // we've already hinted on the preceding pages, so just go on ahead instead of returning an
      // error. The range was valid at the time we started hinting.
      if (end_offset > size_locked()) {
        end_offset = size_locked();
      }

      // If the wait succeeded, cur_offset will now have a backing page, so we need to try the
      // same offset again. Move back a page so the loop increment keeps us at the same offset. In
      // case of failure, simply continue on to the next page, as hints are best effort only.
      if (status == ZX_OK) {
        cur_offset -= PAGE_SIZE;
        continue;
      }
    }

    // Getting here indicates an error was encountered for this page. Simply ignore it and move on
    // to the next page. Hints are best effort anyway.
  }
}

void VmCowPages::MarkAsLatencySensitiveLocked() {
  // Mark this and all our parents as latency sensitive if they haven't already been.
  VmCowPages* cur = this;
  AssertHeld(cur->lock_);
  while (cur && !cur->is_latency_sensitive_) {
    vm_vmo_marked_latency_sensitive.Add(1);
    cur->is_latency_sensitive_ = true;
    cur = cur->parent_.get();
  }
}

void VmCowPages::UnpinLocked(uint64_t offset, uint64_t len, bool allow_gaps) {
  canary_.Assert();

  // verify that the range is within the object
  ASSERT(InRange(offset, len, size_));
  // forbid zero length unpins as zero length pins return errors.
  ASSERT(len != 0);

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->UnpinLocked(offset + parent_offset, len, allow_gaps);
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

#if (DEBUG_ASSERT_IMPLEMENTED)
  // For any pages that have their pin count transition to 0, i.e. become unpinned, we want to
  // perform a range change op. For efficiency track contiguous ranges.
  uint64_t completely_unpin_start = 0;
  uint64_t completely_unpin_len = 0;
#endif

  uint64_t unpin_count = 0;
  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [&](const auto* page, uint64_t off) {
        if (page->IsMarker()) {
          // So far, allow_gaps is only used on contiguous VMOs which have no markers.  We'd need
          // to decide if a marker counts as a gap to allow before removing this assert.
          DEBUG_ASSERT(!allow_gaps);
          return ZX_ERR_NOT_FOUND;
        }
        AssertHeld(lock_);

        // Reference content is not pinned by definition, and so we cannot unpin it.
        ASSERT(!page->IsReference());

        vm_page_t* p = page->Page();
        ASSERT(p->object.pin_count > 0);
        p->object.pin_count--;
        if (p->object.pin_count == 0) {
          MoveToNotPinnedLocked(p, offset);
#if (DEBUG_ASSERT_IMPLEMENTED)
          // Check if the current range can be extended.
          if (completely_unpin_start + completely_unpin_len == off) {
            completely_unpin_len += PAGE_SIZE;
          } else {
            // Complete any existing range and then start again at this offset.
            if (completely_unpin_len > 0) {
              RangeChangeUpdateLocked(completely_unpin_start, completely_unpin_len,
                                      RangeChangeOp::DebugUnpin);
            }
            completely_unpin_start = off;
            completely_unpin_len = PAGE_SIZE;
          }
#endif
        }
        ++unpin_count;
        return ZX_ERR_NEXT;
      },
      [allow_gaps](uint64_t gap_start, uint64_t gap_end) {
        if (!allow_gaps) {
          return ZX_ERR_NOT_FOUND;
        }
        return ZX_ERR_NEXT;
      },
      start_page_offset, end_page_offset);
  ASSERT_MSG(status == ZX_OK, "Tried to unpin an uncommitted page with allow_gaps false");

#if (DEBUG_ASSERT_IMPLEMENTED)
  // Check any leftover range.
  if (completely_unpin_len > 0) {
    RangeChangeUpdateLocked(completely_unpin_start, completely_unpin_len,
                            RangeChangeOp::DebugUnpin);
  }
#endif

  bool overflow = sub_overflow(pinned_page_count_, unpin_count, &pinned_page_count_);
  ASSERT(!overflow);

  return;
}

bool VmCowPages::DebugIsRangePinnedLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  uint64_t pinned_count = 0;
  page_list_.ForEveryPageInRange(
      [&pinned_count](const auto* p, uint64_t off) {
        if (p->IsPage() && p->Page()->object.pin_count > 0) {
          pinned_count++;
          return ZX_ERR_NEXT;
        }
        return ZX_ERR_STOP;
      },
      offset, offset + len);
  return pinned_count == len / PAGE_SIZE;
}

bool VmCowPages::AnyPagesPinnedLocked(uint64_t offset, size_t len) {
  canary_.Assert();
  DEBUG_ASSERT(lock_.lock().IsHeld());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  const uint64_t start_page_offset = offset;
  const uint64_t end_page_offset = offset + len;

  if (pinned_page_count_ == 0) {
    return false;
  }

  bool found_pinned = false;
  page_list_.ForEveryPageInRange(
      [&found_pinned, start_page_offset, end_page_offset](const auto* p, uint64_t off) {
        DEBUG_ASSERT(off >= start_page_offset && off < end_page_offset);
        if (p->IsPage() && p->Page()->object.pin_count > 0) {
          found_pinned = true;
          return ZX_ERR_STOP;
        }
        return ZX_ERR_NEXT;
      },
      start_page_offset, end_page_offset);

  return found_pinned;
}

// Helper function which processes the region visible by both children.
void VmCowPages::ReleaseCowParentPagesLockedHelper(uint64_t start, uint64_t end,
                                                   bool sibling_visible,
                                                   BatchPQRemove* page_remover) {
  // Compute the range in the parent that cur no longer will be able to see.
  const uint64_t parent_range_start = CheckedAdd(start, parent_offset_);
  const uint64_t parent_range_end = CheckedAdd(end, parent_offset_);

  bool skip_split_bits = true;
  if (parent_limit_ <= end) {
    parent_limit_ = ktl::min(start, parent_limit_);
    if (parent_limit_ <= parent_start_limit_) {
      // Setting both to zero is cleaner and makes some asserts easier.
      parent_start_limit_ = 0;
      parent_limit_ = 0;
    }
  } else if (start == parent_start_limit_) {
    parent_start_limit_ = end;
  } else if (sibling_visible) {
    // Split bits and partial cow release are only an issue if this range is also visible to our
    // sibling. If it's not visible then we will always be freeing all pages anyway, no need to
    // worry about split bits. Otherwise if the vmo limits can't be updated, this function will need
    // to use the split bits to release pages in the parent. It also means that ancestor pages in
    // the specified range might end up being released based on their current split bits, instead of
    // through subsequent calls to this function. Therefore parent and all ancestors need to have
    // the partial_cow_release_ flag set to prevent fast merge issues in ::RemoveChildLocked.
    auto cur = this;
    AssertHeld(cur->lock_);
    uint64_t cur_start = start;
    uint64_t cur_end = end;
    while (cur->parent_ && cur_start < cur_end) {
      auto parent = cur->parent_.get();
      AssertHeld(parent->lock_);
      parent->partial_cow_release_ = true;
      cur_start = ktl::max(CheckedAdd(cur_start, cur->parent_offset_), parent->parent_start_limit_);
      cur_end = ktl::min(CheckedAdd(cur_end, cur->parent_offset_), parent->parent_limit_);
      cur = parent;
    }
    skip_split_bits = false;
  }

  // Free any pages that either aren't visible, or were already split into the other child. For
  // pages that haven't been split into the other child, we need to ensure they're univisible.
  AssertHeld(parent_->lock_);
  // We are going to be inserting removed pages into a shared free list. So make sure the parent did
  // not have a page source that was handling frees which would require additional work on the owned
  // pages on top of a simple free to the PMM.
  DEBUG_ASSERT(!parent_->is_source_handling_free_locked());
  parent_->page_list_.RemovePages(
      [skip_split_bits, sibling_visible, page_remover,
       left = this == &parent_->left_child_locked()](VmPageOrMarker* page_or_mark,
                                                     uint64_t offset) {
        if (page_or_mark->IsMarker()) {
          // If this marker is in a range still visible to the sibling then we just leave it, no
          // split bits or anything to be updated. If the sibling cannot see it, then we can clear
          // it.
          if (!sibling_visible) {
            *page_or_mark = VmPageOrMarker::Empty();
          }
          return ZX_ERR_NEXT;
        }
        // If the sibling can still see this page then we need to keep it around, otherwise we can
        // free it. The sibling can see the page if this range is |sibling_visible| and if the
        // sibling hasn't already forked the page, which is recorded in the split bits.
        if (!sibling_visible || left ? page_or_mark->PageOrRefRightSplit()
                                     : page_or_mark->PageOrRefLeftSplit()) {
          page_remover->PushContent(page_or_mark);
          return ZX_ERR_NEXT;
        }
        if (skip_split_bits) {
          // If we were able to update this vmo's parent limit, that made the pages
          // uniaccessible. We clear the split bits to allow ::RemoveChildLocked to efficiently
          // merge vmos without having to worry about pages above parent_limit_.
          page_or_mark->SetPageOrRefLeftSplit(false);
          page_or_mark->SetPageOrRefRightSplit(false);
        } else {
          // Otherwise set the appropriate split bit to make the page uniaccessible.
          if (left) {
            page_or_mark->SetPageOrRefLeftSplit(true);
          } else {
            page_or_mark->SetPageOrRefRightSplit(true);
          }
        }
        return ZX_ERR_NEXT;
      },
      parent_range_start, parent_range_end);
}

void VmCowPages::ReleaseCowParentPagesLocked(uint64_t start, uint64_t end,
                                             BatchPQRemove* page_remover) {
  // This function releases |this| references to any ancestor vmo's COW pages.
  //
  // To do so, we divide |this| parent into three (possibly 0-length) regions: the region
  // which |this| sees but before what the sibling can see, the region where both |this|
  // and its sibling can see, and the region |this| can see but after what the sibling can
  // see. Processing the 2nd region only requires touching the direct parent, since the sibling
  // can see ancestor pages in the region. However, processing the 1st and 3rd regions requires
  // recursively releasing |this| parent's ancestor pages, since those pages are no longer
  // visible through |this| parent.
  //
  // This function processes region 3 (incl. recursively processing the parent), then region 2,
  // then region 1 (incl. recursively processing the parent). Processing is done in reverse order
  // to ensure parent_limit_ is reduced correctly. When processing either regions of type 1 or 3 we
  //  1. walk up the parent and find the largest common slice that all nodes in the hierarchy see
  //     as being of the same type.
  //  2. walk back down (using stack_ direction flags) applying the range update using that final
  //     calculated size
  //  3. reduce the range we are operating on to not include the section we just processed
  //  4. repeat steps 1-3 until range is empty
  // In the worst case it is possible for this algorithm then to be O(N^2) in the depth of the tree.
  // More optimal algorithms probably exist, but this algorithm is sufficient for at the moment as
  // these suboptimal scenarios do not occur in practice.

  // At the top level we continuously attempt to process the range until it is empty.
  while (end > start) {
    // cur_start / cur_end get adjusted as cur moves up/down the parent chain.
    uint64_t cur_start = start;
    uint64_t cur_end = end;
    VmCowPages* cur = this;

    AssertHeld(cur->lock_);
    // First walk up the parent chain as long as there is a visible parent that does not overlap
    // with its sibling.
    while (cur->parent_ && cur->parent_start_limit_ < cur_end && cur_start < cur->parent_limit_) {
      if (cur_end > cur->parent_limit_) {
        // Part of the range sees the parent, and part of it doesn't. As we only process ranges of
        // a single type we first trim the range down to the portion that doesn't see the parent,
        // then next time around the top level loop we will process the portion that does see
        cur_start = cur->parent_limit_;
        DEBUG_ASSERT(cur_start < cur_end);
        break;
      }
      // Trim the start to the portion of the parent it can see.
      cur_start = ktl::max(cur_start, cur->parent_start_limit_);
      DEBUG_ASSERT(cur_start < cur_end);

      // Work out what the overlap with our sibling is
      auto parent = cur->parent_.get();
      AssertHeld(parent->lock_);
      bool left = cur == &parent->left_child_locked();
      auto& other = left ? parent->right_child_locked() : parent->left_child_locked();
      AssertHeld(other.lock_);

      // Project our operating range into our parent.
      const uint64_t our_parent_start = CheckedAdd(cur_start, cur->parent_offset_);
      const uint64_t our_parent_end = CheckedAdd(cur_end, cur->parent_offset_);
      // Project our siblings full range into our parent.
      const uint64_t other_parent_start =
          CheckedAdd(other.parent_offset_, other.parent_start_limit_);
      const uint64_t other_parent_end = CheckedAdd(other.parent_offset_, other.parent_limit_);

      if (other_parent_end >= our_parent_end && other_parent_start < our_parent_end) {
        // At least some of the end of our range overlaps with the sibling. First move up our start
        // to ensure our range is 100% overlapping.
        if (other_parent_start > our_parent_start) {
          cur_start = CheckedAdd(cur_start, other_parent_start - our_parent_start);
          DEBUG_ASSERT(cur_start < cur_end);
        }
        // Free the range that overlaps with the sibling, then we are done walking up as this is the
        // type 2 kind of region. It is safe to process this right now since we are in a terminal
        // state and are leaving the loop, thus we know that this is the final size of the region.
        cur->ReleaseCowParentPagesLockedHelper(cur_start, cur_end, true, page_remover);
        break;
      }
      // End of our range does not see the sibling. First move up our start to ensure we are dealing
      // with a range that is 100% no sibling, and then keep on walking up.
      if (other_parent_end > our_parent_start && other_parent_end < our_parent_end) {
        DEBUG_ASSERT(other_parent_end < our_parent_end);
        cur_start = CheckedAdd(cur_start, other_parent_end - our_parent_start);
        DEBUG_ASSERT(cur_start < cur_end);
      }

      // Record the direction so we can walk about down later.
      parent->stack_.dir_flag = left ? StackDir::Left : StackDir::Right;
      // Don't use our_parent_start as we may have updated cur_start
      cur_start = CheckedAdd(cur_start, cur->parent_offset_);
      cur_end = our_parent_end;
      DEBUG_ASSERT(cur_start < cur_end);
      cur = parent;
    }

    // Every parent that we walked up had no overlap with its siblings. Now that we know the size
    // of the range that we can process we just walk back down processing.
    while (cur != this) {
      // Although we free pages in the parent we operate on the *child*, as that is whose limits
      // we will actually adjust. The ReleaseCowParentPagesLockedHelper will then reach backup to
      // the parent to actually free any pages.
      cur = cur->stack_.dir_flag == StackDir::Left ? &cur->left_child_locked()
                                                   : &cur->right_child_locked();
      AssertHeld(cur->lock_);
      DEBUG_ASSERT(cur_start >= cur->parent_offset_);
      DEBUG_ASSERT(cur_end >= cur->parent_offset_);
      cur_start -= cur->parent_offset_;
      cur_end -= cur->parent_offset_;

      cur->ReleaseCowParentPagesLockedHelper(cur_start, cur_end, false, page_remover);
    }

    // Update the end with the portion we managed to do. Ensuring some basic sanity of the range,
    // most importantly that we processed a non-zero portion to ensure progress.
    DEBUG_ASSERT(cur_start >= start);
    DEBUG_ASSERT(cur_start < end);
    DEBUG_ASSERT(cur_end == end);
    end = cur_start;
  }
}

void VmCowPages::InvalidateReadRequestsLocked(uint64_t offset, uint64_t len) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));

  DEBUG_ASSERT(page_source_);

  const uint64_t start = offset;
  const uint64_t end = offset + len;

  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [](const auto* p, uint64_t off) { return ZX_ERR_NEXT; },
      [this](uint64_t gap_start, uint64_t gap_end) {
        page_source_->OnPagesSupplied(gap_start, gap_end - gap_start);
        return ZX_ERR_NEXT;
      },
      start, end);
  DEBUG_ASSERT(status == ZX_OK);
}

void VmCowPages::InvalidateDirtyRequestsLocked(uint64_t offset, uint64_t len) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));

  DEBUG_ASSERT(is_source_preserving_page_content());
  DEBUG_ASSERT(page_source_->ShouldTrapDirtyTransitions());

  const uint64_t start = offset;
  const uint64_t end = offset + len;

  // Pages before supply_zero_offset_ in state Clean and AwaitingClean might be waiting on
  // DIRTY requests. Pages after supply_zero_offset_ in AwaitingClean might be waiting on
  // DIRTY requests. So we need to traverse the entire range to find such pages.
  zx_status_t status = page_list_.ForEveryPageAndContiguousRunInRange(
      [supply_zero_offset = supply_zero_offset_](const VmPageOrMarker* p, uint64_t off) {
        // We can't have any markers after supply_zero_offset_.
        DEBUG_ASSERT(off < supply_zero_offset || p->IsPageOrRef());
        // A marker is a clean zero page and might have an outstanding DIRTY request.
        if (p->IsMarker()) {
          return true;
        }
        // Although a reference is implied to be clean, VMO backed by a page source should never
        // have references.
        DEBUG_ASSERT(!p->IsReference());

        vm_page_t* page = p->Page();
        DEBUG_ASSERT(is_page_dirty_tracked(page));
        // We can only have un-Clean non-loaned pages after supply_zero_offset_.
        DEBUG_ASSERT(off < supply_zero_offset || !is_page_clean(page));
        DEBUG_ASSERT(off < supply_zero_offset || !page->is_loaned());

        // A page that is not Dirty already might have an outstanding DIRTY request.
        if (!is_page_dirty(page)) {
          return true;
        }
        // Otherwise the page should already be Dirty.
        DEBUG_ASSERT(is_page_dirty(page));
        return false;
      },
      [](const VmPageOrMarker* p, uint64_t off) {
        // Nothing to update for the page as we're not actually marking it Dirty.
        return ZX_ERR_NEXT;
      },
      [this](uint64_t start, uint64_t end) {
        // Resolve any DIRTY requests in this contiguous range.
        page_source_->OnPagesDirtied(start, end - start);
        return ZX_ERR_NEXT;
      },
      start, end);
  // We don't expect an error from the traversal.
  DEBUG_ASSERT(status == ZX_OK);

  // Now resolve DIRTY requests for any gaps. After request generation, pages could either
  // have been evicted, or supply_zero_offset_ advanced on writeback, leading to gaps. So it
  // is possible for gaps to have outstanding DIRTY requests.
  status = page_list_.ForEveryPageAndGapInRange(
      [](const VmPageOrMarker* p, uint64_t off) {
        // Nothing to do for pages. We already handled them above.
        return ZX_ERR_NEXT;
      },
      [this](uint64_t gap_start, uint64_t gap_end) {
        // Resolve any DIRTY requests in this gap.
        page_source_->OnPagesDirtied(gap_start, gap_end - gap_start);
        return ZX_ERR_NEXT;
      },
      start, end);
  // We don't expect an error from the traversal.
  DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t VmCowPages::ResizeLocked(uint64_t s) {
  canary_.Assert();

  LTRACEF("vmcp %p, size %" PRIu64 "\n", this, s);

  // make sure everything is aligned before we get started
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(s));
  DEBUG_ASSERT(!is_slice_locked());

  // We stack-own loaned pages from removal until freed.
  __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

  // see if we're shrinking or expanding the vmo
  if (s < size_) {
    // shrinking
    const uint64_t start = s;
    const uint64_t end = size_;
    const uint64_t len = end - start;

    // bail if there are any pinned pages in the range we're trimming
    if (AnyPagesPinnedLocked(start, len)) {
      return ZX_ERR_BAD_STATE;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(start, len, RangeChangeOp::Unmap);

    // Resolve any outstanding page requests tracked by the page source that are now out-of-bounds.
    if (page_source_) {
      // Tell the page source that any non-resident pages that are now out-of-bounds
      // were supplied, to ensure that any reads of those pages get woken up.
      InvalidateReadRequestsLocked(start, len);

      // If DIRTY requests are supported, also tell the page source that any non-Dirty pages that
      // are now out-of-bounds were dirtied (without actually dirtying them), to ensure that any
      // threads blocked on DIRTY requests for those pages get woken up.
      if (is_source_preserving_page_content() && page_source_->ShouldTrapDirtyTransitions()) {
        InvalidateDirtyRequestsLocked(start, len);
      }
    }

    // If the page source is preserving content, supply_zero_offset_ and/or
    // awaiting_clean_zero_range_end_ might need updating.
    if (is_source_preserving_page_content()) {
      if (s < supply_zero_offset_) {
        // If the new size is smaller than supply_zero_offset_, supply_zero_offset_ can be clipped
        // to the new size. The supply_zero_offset_ is used to supply zero pages at the tail end of
        // the VMO and must therefore fall within the VMO size.
        // This will also update awaiting_clean_zero_range_end_ if required.
        UpdateSupplyZeroOffsetLocked(s);
        // We should have reset the AwaitingClean zero range as it is out of bounds now.
        DEBUG_ASSERT(awaiting_clean_zero_range_end_ == 0);
      } else {
        // We might need to trim the AwaitingClean zero range [supply_zero_offset_,
        // awaiting_clean_zero_range_end_) if the new size falls partway into that range.
        ConsiderTrimAwaitingCleanZeroRangeLocked(s);
      }
    }

    // We might need to free pages from an ancestor and/or this object.
    list_node_t freed_list;
    list_initialize(&freed_list);
    __UNINITIALIZED BatchPQRemove page_remover(&freed_list);

    bool hidden_parent = false;
    if (parent_) {
      AssertHeld(parent_->lock_ref());
      hidden_parent = parent_->is_hidden_locked();
    }
    if (hidden_parent) {
      // Release any COW pages that are no longer necessary. This will also
      // update the parent limit.
      ReleaseCowParentPagesLocked(start, end, &page_remover);

      // Flush the page remover and free the pages, so that we don't mix ownership of ancestor pages
      // with pages removed from this object below.
      page_remover.Flush();
      FreePagesLocked(&freed_list, /*freeing_owned_pages=*/false);

      // Validate that the parent limit was correctly updated as it should never remain larger than
      // our actual size.
      DEBUG_ASSERT(parent_limit_ <= s);
    } else {
      parent_limit_ = ktl::min(parent_limit_, s);
    }
    // If the tail of a parent disappears, the children shouldn't be able to see that region
    // again, even if the parent is later reenlarged. So update the child parent limits.
    UpdateChildParentLimitsLocked(s);

    // We should not have any outstanding pages to free as we flushed ancestor pages already. So
    // this flush should be a no-op.
    page_remover.Flush();
    DEBUG_ASSERT(list_length(&freed_list) == 0);

    // Remove and free pages from this object.
    page_list_.RemovePages(page_remover.RemovePagesCallback(), start, end);
    page_remover.Flush();
    FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);

  } else if (s > size_) {
    uint64_t temp;
    // Check that this VMOs new size would not cause it to overflow if projected onto the root.
    bool overflow = add_overflow(root_parent_offset_, s, &temp);
    if (overflow) {
      return ZX_ERR_INVALID_ARGS;
    }
    // expanding
    // figure the starting and ending page offset that is affected
    const uint64_t start = size_;
    const uint64_t end = s;
    const uint64_t len = end - start;

    // inform all our children or mapping that there's new bits
    RangeChangeUpdateLocked(start, len, RangeChangeOp::Unmap);
  }

  // save bytewise size
  size_ = s;

  IncrementHierarchyGenerationCountLocked();

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  return ZX_OK;
}

void VmCowPages::UpdateChildParentLimitsLocked(uint64_t new_size) {
  // Note that a child's parent_limit_ will limit that child's descendants' views into
  // this vmo, so this method only needs to touch the direct children.
  for (auto& child : children_list_) {
    AssertHeld(child.lock_);
    if (new_size < child.parent_offset_) {
      child.parent_limit_ = 0;
    } else {
      child.parent_limit_ = ktl::min(child.parent_limit_, new_size - child.parent_offset_);
    }
  }
}

zx_status_t VmCowPages::LookupLocked(uint64_t offset, uint64_t len,
                                     VmObject::LookupFunction lookup_fn) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // verify that the range is within the object
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (is_slice_locked()) {
    DEBUG_ASSERT(parent_);
    AssertHeld(parent_->lock_ref());
    // Slices are always hung off a non-slice parent, so we know we only need to walk up one level.
    DEBUG_ASSERT(!parent_->is_slice_locked());
    return parent_->LookupLocked(
        offset + parent_offset_, len,
        [&lookup_fn, parent_offset = parent_offset_](uint64_t offset, paddr_t pa) {
          // Need to undo the parent_offset before forwarding to the lookup_fn, who is ignorant of
          // slices.
          return lookup_fn(offset - parent_offset, pa);
        });
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  return page_list_.ForEveryPageInRange(
      [&lookup_fn](const auto* p, uint64_t off) {
        if (!p->IsPage()) {
          // Skip non pages.
          return ZX_ERR_NEXT;
        }
        paddr_t pa = p->Page()->paddr();
        return lookup_fn(off, pa);
      },
      start_page_offset, end_page_offset);
}

zx_status_t VmCowPages::LookupReadableLocked(uint64_t offset, uint64_t len,
                                             LookupReadableFunction lookup_fn) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // verify that the range is within the object
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (is_slice_locked()) {
    DEBUG_ASSERT(parent_);
    AssertHeld(parent_->lock_ref());
    // Slices are always hung off a non-slice parent, so we know we only need to walk up one level.
    DEBUG_ASSERT(!parent_->is_slice_locked());
    return parent_->LookupReadableLocked(
        offset + parent_offset_, len,
        [&lookup_fn, parent_offset = parent_offset_](uint64_t offset, paddr_t pa) {
          // Need to undo the parent_offset before forwarding to the lookup_fn, who is ignorant of
          // slices.
          return lookup_fn(offset - parent_offset, pa);
        });
  }

  uint64_t current_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  while (current_page_offset != end_page_offset) {
    // Attempt to process any pages we have first.
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&lookup_fn, &current_page_offset](const VmPageOrMarker* page_or_marker, uint64_t offset) {
          DEBUG_ASSERT(offset == current_page_offset);
          current_page_offset += PAGE_SIZE;
          if (!page_or_marker->IsPage()) {
            return ZX_ERR_NEXT;
          }
          return lookup_fn(offset, page_or_marker->Page()->paddr());
        },
        [](uint64_t gap_start, uint64_t gap_end) { return ZX_ERR_STOP; }, current_page_offset,
        end_page_offset);

    // Check if we've processed the whole range.
    if (current_page_offset == end_page_offset) {
      break;
    }

    // See if any of our parents have the content.
    VmCowPages* owner = nullptr;
    uint64_t owner_offset = 0;
    uint64_t owner_length = end_page_offset - current_page_offset;

    // We do not care about the return value, all we are interested in is the populated out
    // variables that we pass in.
    FindInitialPageContentLocked(current_page_offset, &owner, &owner_offset, &owner_length);

    // This should always get filled out.
    DEBUG_ASSERT(owner_length > 0);
    DEBUG_ASSERT(owner);

    // Iterate over any potential content.
    AssertHeld(owner->lock_ref());
    status = owner->page_list_.ForEveryPageInRange(
        [&lookup_fn, current_page_offset, owner_offset](const VmPageOrMarker* page_or_marker,
                                                        uint64_t offset) {
          if (!page_or_marker->IsPage()) {
            return ZX_ERR_NEXT;
          }
          return lookup_fn(offset - owner_offset + current_page_offset,
                           page_or_marker->Page()->paddr());
        },
        owner_offset, owner_offset + owner_length);
    if (status != ZX_OK || status != ZX_ERR_NEXT) {
      return status;
    }

    current_page_offset += owner_length;
  }
  return ZX_OK;
}

zx_status_t VmCowPages::TakePagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (AnyPagesPinnedLocked(offset, len) || parent_ || page_source_) {
    return ZX_ERR_BAD_STATE;
  }

  // This is only used by the userpager API, which has significant restrictions on
  // what sorts of vmos are acceptable. If splice starts being used in more places,
  // then this restriction might need to be lifted.
  // TODO: Check that the region is locked once locking is implemented
  if (children_list_len_) {
    return ZX_ERR_BAD_STATE;
  }

  page_list_.ForEveryPageInRange(
      [](const auto* p, uint64_t off) {
        if (p->IsPage()) {
          DEBUG_ASSERT(p->Page()->object.pin_count == 0);
          pmm_page_queues()->Remove(p->Page());
        }
        // Reference types are permitted in the VmPageSpliceList, it is up to the receiver of the
        // pages to reject or otherwise deal with them.
        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  *pages = page_list_.TakePages(offset, len);

  RangeChangeUpdateLocked(offset, len, RangeChangeOp::Unmap);

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  return ZX_OK;
}

zx_status_t VmCowPages::SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages,
                                    bool new_zeroed_pages, uint64_t* supplied_len,
                                    LazyPageRequest* page_request) {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  IncrementHierarchyGenerationCountLocked();
  return SupplyPagesLocked(offset, len, pages, new_zeroed_pages, supplied_len, page_request);
}

zx_status_t VmCowPages::SupplyPagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages,
                                          bool new_zeroed_pages, uint64_t* supplied_len,
                                          LazyPageRequest* page_request) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(supplied_len);

  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    *supplied_len = 0;
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint64_t start = offset;
  const uint64_t end = offset + len;

  // We stack-own loaned pages below from allocation for page replacement to AddPageLocked().
  __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

  list_node freed_list;
  list_initialize(&freed_list);

  // [new_pages_start, new_pages_start + new_pages_len) tracks the current run of
  // consecutive new pages added to this vmo.
  uint64_t new_pages_start = offset;
  uint64_t new_pages_len = 0;
  zx_status_t status = ZX_OK;
  while (!pages->IsDone()) {
    VmPageOrMarker src_page = pages->Pop();

    // The pager API does not allow the source VMO of supply pages to have a page source, so we can
    // assume that any empty pages are zeroes and insert explicit markers here. We need to insert
    // explicit markers to actually resolve the pager fault.
    if (src_page.IsEmpty()) {
      src_page = VmPageOrMarker::Marker();
    }

    // With a PageSource only Pages are supported, so convert any refs to real pages.
    if (src_page.IsReference()) {
      status = MakePageFromReference(VmPageOrMarkerRef(&src_page), page_request);
      if (status != ZX_OK) {
        break;
      }
    }
    DEBUG_ASSERT(!src_page.IsReference());

    // A newly supplied page starts off as Clean.
    if (src_page.IsPage() && is_source_preserving_page_content()) {
      UpdateDirtyStateLocked(src_page.Page(), offset, DirtyState::Clean,
                             /*is_pending_add=*/true);
    }

    if (can_borrow_locked() && src_page.IsPage() &&
        pmm_physical_page_borrowing_config()->is_borrowing_in_supplypages_enabled()) {
      // Assert some things we implicitly know are true (currently).  We can avoid explicitly
      // checking these in the if condition for now.
      DEBUG_ASSERT(!is_source_supplying_specific_physical_pages());
      DEBUG_ASSERT(!src_page.Page()->is_loaned());
      DEBUG_ASSERT(!new_zeroed_pages);
      // Try to replace src_page with a loaned page.  We allocate the loaned page one page at a time
      // to avoid failing the allocation due to asking for more loaned pages than there are free
      // loaned pages. Loaned page allocations will always precisely succeed or fail and the
      // CAN_WAIT flag cannot be combined and so we remove it if it exists.
      uint32_t pmm_alloc_flags = pmm_alloc_flags_;
      pmm_alloc_flags &= ~PMM_ALLOC_FLAG_CAN_WAIT;
      pmm_alloc_flags |= PMM_ALLOC_FLAG_MUST_BORROW | PMM_ALLOC_FLAG_CAN_BORROW;
      vm_page_t* new_page;
      zx_status_t alloc_status = pmm_alloc_page(pmm_alloc_flags, &new_page);
      // If we got a loaned page, replace the page in src_page, else just continue with src_page
      // unmodified since pmm has no more loaned free pages or
      // !is_borrowing_in_supplypages_enabled().
      if (alloc_status == ZX_OK) {
        InitializeVmPage(new_page);
        CopyPageForReplacementLocked(new_page, src_page.Page());
        vm_page_t* old_page = src_page.ReleasePage();
        list_add_tail(&freed_list, &old_page->queue_node);
        src_page = VmPageOrMarker::Page(new_page);
      }
      DEBUG_ASSERT(src_page.IsPage());
    }

    // Defer individual range updates so we can do them in blocks.
    if (new_zeroed_pages) {
      // When new_zeroed_pages is true, we need to call InitializeVmPage(), which AddNewPageLocked()
      // will do.
      // We only want to populate offsets that have true absence of content, so do not overwrite
      // anything in the page list.
      DEBUG_ASSERT(src_page.IsPage());
      status = AddNewPageLocked(offset, src_page.Page(), CanOverwriteContent::None, nullptr,
                                /*zero=*/false, /*do_range_update=*/false);
      if (status == ZX_OK) {
        // The page was successfully added, but we still have a copy in the src_page, so we need to
        // release it, however need to store the result in a temporary as we are required to use the
        // result of ReleasePage.
        [[maybe_unused]] vm_page_t* unused = src_page.ReleasePage();
      }
    } else {
      // When new_zeroed_pages is false, we don't need InitializeVmPage(), so we use
      // AddPageLocked().
      // We only want to populate offsets that have true absence of content, so do not overwrite
      // anything in the page list.
      status = AddPageLocked(&src_page, offset, CanOverwriteContent::None, nullptr,
                             /*do_range_update=*/false);
    }

    if (status == ZX_OK) {
      new_pages_len += PAGE_SIZE;
    } else {
      if (src_page.IsPageOrRef()) {
        DEBUG_ASSERT(src_page.IsPage());
        vm_page_t* page = src_page.ReleasePage();
        DEBUG_ASSERT(!list_in_list(&page->queue_node));
        list_add_tail(&freed_list, &page->queue_node);
      }

      if (likely(status == ZX_ERR_ALREADY_EXISTS)) {
        status = ZX_OK;

        // We hit the end of a run of absent pages, so notify the page source
        // of any new pages that were added and reset the tracking variables.
        if (new_pages_len) {
          RangeChangeUpdateLocked(new_pages_start, new_pages_len, RangeChangeOp::Unmap);
          page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
        }
        new_pages_start = offset + PAGE_SIZE;
        new_pages_len = 0;
      } else {
        break;
      }
    }
    offset += PAGE_SIZE;

    DEBUG_ASSERT(new_pages_start + new_pages_len <= end);
  }
  // Unless there was an error and we exited the loop early, then there should have been the correct
  // number of pages in the splice list.
  DEBUG_ASSERT(offset == end || status != ZX_OK);
  if (new_pages_len) {
    RangeChangeUpdateLocked(new_pages_start, new_pages_len, RangeChangeOp::Unmap);
    page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
  }

  if (!list_is_empty(&freed_list)) {
    // Even though we did not insert these pages successfully, we had logical ownership of them.
    FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);
  }

  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());

  *supplied_len = offset - start;
  return status;
}

// This is a transient operation used only to fail currently outstanding page requests. It does not
// alter the state of the VMO, or any pages that might have already been populated within the
// specified range.
//
// If certain pages in this range are populated, we must have done so via a previous SupplyPages()
// call that succeeded. So it might be fine for clients to continue accessing them, despite the
// larger range having failed.
//
// TODO(rashaeqbal): If we support a more permanent failure mode in the future, we will need to free
// populated pages in the specified range, and possibly detach the VMO from the page source.
zx_status_t VmCowPages::FailPageRequestsLocked(uint64_t offset, uint64_t len,
                                               zx_status_t error_status) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  ASSERT(page_source_);

  if (!PageSource::IsValidInternalFailureCode(error_status)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  page_source_->OnPagesFailed(offset, len, error_status);
  return ZX_OK;
}

zx_status_t VmCowPages::DirtyPagesLocked(uint64_t offset, uint64_t len, list_node_t* alloc_list,
                                         LazyPageRequest* page_request) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  ASSERT(page_source_);

  if (!page_source_->ShouldTrapDirtyTransitions()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  DEBUG_ASSERT(is_source_preserving_page_content());

  const uint64_t start_offset = offset;
  const uint64_t end_offset = offset + len;

  if (start_offset > size_locked()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Overflow check.
  if (end_offset < start_offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // After the above checks, the page source has tried to respond correctly to a range of dirty
  // requests, so the kernel should resolve those outstanding dirty requests, even in the failure
  // case. From a returned error, the page source currently has no ability to detect which ranges
  // caused the error, so the kernel should either completely succeed or fail the request instead of
  // holding onto a partial outstanding request that will block pager progress.
  auto invalidate_requests_on_error = fit::defer([this, len, start_offset] {
    AssertHeld(lock_);
    DEBUG_ASSERT(size_locked() >= start_offset);

    uint64_t invalidate_len = ktl::min(size_locked() - start_offset, len);
    InvalidateDirtyRequestsLocked(start_offset, invalidate_len);
  });

  // The page source may have tried to mark a larger range than necessary as dirty. Invalidate the
  // requests and return an error.
  if (end_offset > size_locked()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // If any of the pages in the range are zero page markers (Clean zero pages), they need to be
  // forked in order to be dirtied (written to). Find the number of such pages that need to be
  // allocated. We might also need to allocate zero pages to replace empty slots starting at
  // supply_zero_offset_. See comment near DIRTY request generation in PrepareForWriteLocked for
  // more details.
  size_t zero_pages_count = 0;

  // First consider the portion of the range that ends before supply_zero_offset_.
  // We don't have a range to consider here if offset was greater than supply_zero_offset_.
  if (start_offset < supply_zero_offset_) {
    const uint64_t end = ktl::min(supply_zero_offset_, end_offset);
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&zero_pages_count](const VmPageOrMarker* p, uint64_t off) {
          if (p->IsMarker()) {
            zero_pages_count++;
          }
          DEBUG_ASSERT(!p->IsReference());
          return ZX_ERR_NEXT;
        },
        [](uint64_t start, uint64_t end) {
          // A gap indicates a page that has not been supplied yet. It will need to be supplied
          // first. Although we will never generate a DIRTY request for absent pages before
          // supply_zero_offset_ in the first place, it is still possible for a clean page to get
          // evicted after the DIRTY request was generated. It is also possible for the
          // supply_zero_offset_ to get advanced itself due to a racing writeback, such that an old
          // DIRTY request (for uncommitted pages beyond supply_zero_offset_) now starts before the
          // advanced supply_zero_offset_.
          //
          // Spuriously resolve the DIRTY page request, and let the waiter(s) retry looking up the
          // page, which will generate a READ request first to supply the missing page.
          return ZX_ERR_NOT_FOUND;
        },
        start_offset, end);

    if (status != ZX_OK) {
      return status;
    }
  }

  // Now consider the portion of the range that starts at/after supply_zero_offset_.
  // [offset, offset + len) might have fallen entirely before supply_zero_offset_, in which case we
  // have no remaining portion to consider here.
  if (supply_zero_offset_ < end_offset) {
    const uint64_t start = ktl::max(start_offset, supply_zero_offset_);
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [](const VmPageOrMarker* p, uint64_t off) {
          // Nothing to do if a page is found except assert a few things we know.
          DEBUG_ASSERT(p->IsPage());
          DEBUG_ASSERT(is_page_dirty_tracked(p->Page()));
          DEBUG_ASSERT(!is_page_clean(p->Page()));
          DEBUG_ASSERT(!p->Page()->is_loaned());
          return ZX_ERR_NEXT;
        },
        [&zero_pages_count](uint64_t start, uint64_t end) {
          zero_pages_count += ((end - start) / PAGE_SIZE);
          return ZX_ERR_NEXT;
        },
        start, end_offset);
    // We don't expect an error from the traversal.
    DEBUG_ASSERT(status == ZX_OK);
  }

  // Utilize the already allocated pages in alloc_list.
  uint64_t alloc_list_len = list_length(alloc_list);
  zero_pages_count = zero_pages_count > alloc_list_len ? zero_pages_count - alloc_list_len : 0;

  // Allocate the number of zero pages required upfront, so that we can fail the call early if the
  // page allocation fails.
  if (zero_pages_count > 0) {
    // First try to allocate all the pages at once. This is an optimization and avoids repeated
    // calls to the PMM to allocate single pages. If the PMM returns ZX_ERR_SHOULD_WAIT, fall back
    // to allocating one page at a time below, giving reclamation strategies a better chance to
    // catch up with incoming allocation requests.
    zx_status_t status = pmm_alloc_pages(zero_pages_count, pmm_alloc_flags_, alloc_list);
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      return status;
    }

    // Fall back to allocating a single page at a time. We want to do this before we can start
    // inserting pages into the page list, to avoid rolling back any pages we inserted but could not
    // dirty in case we fail partway after having inserted some pages into the page list. Rolling
    // back like this can lead to a livelock where we are constantly allocating some pages, freeing
    // them, waiting on the page_request, and then repeating.
    //
    // If allocations do fail partway here, we will have accumulated the allocated pages in
    // alloc_list, so we will be able to reuse them on a subsequent call to DirtyPagesLocked. This
    // ensures we are making forward progress across successive calls.
    while (zero_pages_count > 0) {
      vm_page_t* new_page;
      status = pmm_alloc_page(pmm_alloc_flags_, &new_page);
      // If single page allocation fails, bubble up the failure.
      if (status != ZX_OK) {
        // If asked to wait, fill in the page request for the caller to wait on.
        if (status == ZX_ERR_SHOULD_WAIT) {
          DEBUG_ASSERT(page_request);
          status = AnonymousPageRequester::Get().FillRequest(page_request->get());
          DEBUG_ASSERT(status == ZX_ERR_SHOULD_WAIT);
          return status;
        }
        // Map all allocation failures except ZX_ERR_SHOULD_WAIT to ZX_ERR_NO_MEMORY.
        return ZX_ERR_NO_MEMORY;
      }
      list_add_tail(alloc_list, &new_page->queue_node);
      zero_pages_count--;
    }

    // Increment the generation count as we're going to be inserting new pages.
    IncrementHierarchyGenerationCountLocked();

    // Install newly allocated pages in place of the zero page markers before supply_zero_offset_.
    if (start_offset < supply_zero_offset_) {
      const uint64_t end = ktl::min(supply_zero_offset_, end_offset);
      status = page_list_.ForEveryPageInRange(
          [this, &alloc_list](const VmPageOrMarker* p, uint64_t off) {
            if (p->IsMarker()) {
              DEBUG_ASSERT(!list_is_empty(alloc_list));
              AssertHeld(lock_);

              // AddNewPageLocked will also zero the page and update any mappings.
              //
              // TODO(rashaeqbal): Depending on how often we end up forking zero markers, we might
              // want to pass do_range_udpate = false, and defer updates until later, so we can
              // perform a single batch update.
              zx_status_t status =
                  AddNewPageLocked(off, list_remove_head_type(alloc_list, vm_page, queue_node),
                                   CanOverwriteContent::Zero, nullptr);
              // AddNewPageLocked will not fail with ZX_ERR_ALREADY_EXISTS as we can overwrite
              // markers with OverwriteInitialContent, nor with ZX_ERR_NO_MEMORY as we don't need to
              // allocate a new slot in the page list, we're simply replacing its content.
              ASSERT(status == ZX_OK);
            }
            return ZX_ERR_NEXT;
          },
          start_offset, end);

      // We don't expect an error from the traversal.
      DEBUG_ASSERT(status == ZX_OK);
    }

    // Deferred cleanup if inserting pages starting at supply_zero_offset_ fails partway below.
    // Pages starting at supply_zero_offset_ can only be Dirty. So if we added any new pages with
    // the intention of dirtying them in this function, but we could not successfully do so (because
    // allocation for a page list node failed part of the way), we need to roll back.
    //
    // Note that this roll back is fine as it does not risk forward progress, as opposed to rolling
    // back in case of ZX_ERR_SHOULD_WAIT. The caller will not retry with an error status of
    // ZX_ERR_NO_MEMORY.
    auto zero_offset_cleanup = fit::defer([this, end_offset]() {
      AssertHeld(lock_);
      if (supply_zero_offset_ < end_offset) {
        list_node_t freed_list;
        list_initialize(&freed_list);

        page_list_.RemovePages(
            [&freed_list](VmPageOrMarker* p, uint64_t off) {
              DEBUG_ASSERT(p->IsPage());
              vm_page_t* page = p->Page();
              DEBUG_ASSERT(is_page_dirty_tracked(page));
              DEBUG_ASSERT(!page->is_loaned());
              // The only Clean pages will be the new ones we inserted in this function.
              if (is_page_clean(page)) {
                vm_page_t* released_page = p->ReleasePage();
                DEBUG_ASSERT(released_page == page);
                DEBUG_ASSERT(page->object.pin_count == 0);
                pmm_page_queues()->Remove(page);
                DEBUG_ASSERT(!list_in_list(&page->queue_node));
                list_add_tail(&freed_list, &page->queue_node);
              }
              return ZX_ERR_NEXT;
            },
            supply_zero_offset_, end_offset);

        if (!list_is_empty(&freed_list)) {
          FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);
        }
      }
      VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
    });

    // Install zero pages in gaps starting at supply_zero_offset_.
    for (uint64_t off = ktl::max(start_offset, supply_zero_offset_); off < end_offset;
         off += PAGE_SIZE) {
      auto slot = page_list_.Lookup(off);
      if (slot && !slot->IsEmpty()) {
        // We can only find un-Clean pages beyond supply_zero_offset_.
        DEBUG_ASSERT(slot->IsPage());
        DEBUG_ASSERT(is_page_dirty_tracked(slot->Page()));
        DEBUG_ASSERT(!is_page_clean(slot->Page()));
        DEBUG_ASSERT(!slot->Page()->is_loaned());
        continue;
      }

      DEBUG_ASSERT(!list_is_empty(alloc_list));
      // AddNewPageLocked will also zero the page and update any mappings.
      status = AddNewPageLocked(off, list_remove_head_type(alloc_list, vm_page, queue_node),
                                CanOverwriteContent::Zero, nullptr);
      // We know that there was no page here so AddNewPageLocked will not fail with
      // ZX_ERR_ALREADY_EXISTS. The only possible error is ZX_ERR_NO_MEMORY if we failed to allocate
      // the slot.
      if (status == ZX_ERR_NO_MEMORY) {
        return status;
      }
      ASSERT(status == ZX_OK);
    }

    // We were able to successfully insert all the required pages. Cancel the cleanup.
    zero_offset_cleanup.cancel();
  }

  // After this point, we have to mark all the requested pages Dirty *atomically*. The user pager
  // might be tracking filesystem space reservations based on the success / failure of this call. So
  // if we fail partway, the user pager might think that no pages in the specified range have been
  // dirtied, which would be incorrect. If there are any conditions that would cause us to fail,
  // evaluate those before reaching here, so that we can return the failure early before starting to
  // mark pages Dirty.

  zx_status_t status = page_list_.ForEveryPageAndContiguousRunInRange(
      [](const VmPageOrMarker* p, uint64_t off) {
        DEBUG_ASSERT(!p->IsReference());
        if (p->IsPage()) {
          vm_page_t* page = p->Page();
          DEBUG_ASSERT(is_page_dirty_tracked(page));
          DEBUG_ASSERT(is_page_clean(page) || !page->is_loaned());
          return !is_page_dirty(page);
        }
        return false;
      },
      [this](const VmPageOrMarker* p, uint64_t off) {
        DEBUG_ASSERT(p->IsPage());
        vm_page_t* page = p->Page();
        DEBUG_ASSERT(is_page_dirty_tracked(page));
        DEBUG_ASSERT(!is_page_dirty(page));
        AssertHeld(lock_);
        UpdateDirtyStateLocked(page, off, DirtyState::Dirty);
        return ZX_ERR_NEXT;
      },
      [this](uint64_t start, uint64_t end) {
        page_source_->OnPagesDirtied(start, end - start);
        return ZX_ERR_NEXT;
      },
      start_offset, end_offset);
  // We don't expect a failure from the traversal.
  DEBUG_ASSERT(status == ZX_OK);

  // All pages have been dirtied successfully, so cancel the cleanup on error.
  invalidate_requests_on_error.cancel();

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  return status;
}

void VmCowPages::TryAdvanceSupplyZeroOffsetLocked(uint64_t start_offset, uint64_t end_offset) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(end_offset));

  if (supply_zero_offset_ >= start_offset && supply_zero_offset_ < end_offset) {
    uint64_t new_zero_offset = supply_zero_offset_;
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&new_zero_offset](const VmPageOrMarker* p, uint64_t off) {
          ASSERT(p->IsPage());
          vm_page_t* page = p->Page();
          ASSERT(is_page_dirty_tracked(page));
          ASSERT(!is_page_clean(page));
          DEBUG_ASSERT(!page->is_loaned());
          new_zero_offset += PAGE_SIZE;
          return ZX_ERR_NEXT;
        },
        [](uint64_t start, uint64_t end) {
          // Bail if we found a gap.
          return ZX_ERR_STOP;
        },
        supply_zero_offset_, end_offset);
    // We don't expect a failure from the traversal.
    DEBUG_ASSERT(status == ZX_OK);

    // Advance supply_zero_offset_.
    // This will also update awaiting_clean_zero_range_end_ if required.
    UpdateSupplyZeroOffsetLocked(new_zero_offset);
  }

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
}

zx_status_t VmCowPages::EnumerateDirtyRangesLocked(uint64_t offset, uint64_t len,
                                                   DirtyRangeEnumerateFunction&& dirty_range_fn) {
  canary_.Assert();

  // Dirty pages are only tracked if the page source preserves content.
  if (!is_source_preserving_page_content()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint64_t start_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_offset = ROUNDUP(offset + len, PAGE_SIZE);

  // If supply_zero_offset_ falls in the range being enumerated, try to advance supply_zero_offset_
  // over any pages that might have been committed immediately after it. This gives us the
  // opportunity to coalesce committed pages across supply_zero_offset_ into a single dirty range.
  //
  // Cap the amount of work by only considering advancing supply_zero_offset_ until end_offset. We
  // will be iterating over the range [start_offset, end_offset) to enumerate dirty ranges anyway,
  // so attempting to advance supply_zero_offset_ within this range still keeps the order of work
  // performed in this call the same.
  TryAdvanceSupplyZeroOffsetLocked(start_offset, end_offset);

  // First consider the portion of the range that ends before supply_zero_offset_.
  // We don't have a range to consider here if offset was greater than supply_zero_offset_.
  if (start_offset < supply_zero_offset_) {
    const uint64_t end = ktl::min(supply_zero_offset_, end_offset);
    zx_status_t status = page_list_.ForEveryPageAndContiguousRunInRange(
        [](const VmPageOrMarker* p, uint64_t off) {
          // Enumerate both AwaitingClean and Dirty pages, i.e. anything that is not Clean.
          // AwaitingClean pages are "dirty" too for the purposes of this enumeration, since their
          // modified contents are still in the process of being written back.
          DEBUG_ASSERT(!p->IsReference());
          if (p->IsPage()) {
            vm_page_t* page = p->Page();
            DEBUG_ASSERT(is_page_dirty_tracked(page));
            DEBUG_ASSERT(is_page_clean(page) || !page->is_loaned());
            return !is_page_clean(page);
          }
          return false;
        },
        [](const VmPageOrMarker* p, uint64_t off) {
          DEBUG_ASSERT(p->IsPage());
          vm_page_t* page = p->Page();
          DEBUG_ASSERT(is_page_dirty_tracked(page));
          DEBUG_ASSERT(!is_page_clean(page));
          DEBUG_ASSERT(!page->is_loaned());
          DEBUG_ASSERT(page->object.get_page_offset() == off);
          return ZX_ERR_NEXT;
        },
        [&dirty_range_fn](uint64_t start, uint64_t end) {
          return dirty_range_fn(start, end - start, /*range_is_zero=*/false);
        },
        start_offset, end);

    if (status != ZX_OK) {
      return status;
    }
  }

  // Now consider the portion of the range that starts at/after supply_zero_offset_. All pages
  // beyond supply_zero_offset_ must be reported Dirty so that they can be written back. Gaps must
  // be reported as zero so that writing them back may be optimized.
  // [offset, offset + len) might have fallen entirely before supply_zero_offset_, in which case we
  // have no remaining portion to consider here.
  if (supply_zero_offset_ < end_offset) {
    const uint64_t start = ktl::max(start_offset, supply_zero_offset_);

    // Counters to track a potential run of committed pages.
    uint64_t committed_start = start;
    uint64_t committed_len = 0;

    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&committed_start, &committed_len](const VmPageOrMarker* p, uint64_t off) {
          // We can only find un-Clean pages beyond supply_zero_offset_. There can be no markers as
          // they represent Clean zero pages.
          ASSERT(p->IsPage());
          vm_page_t* page = p->Page();
          ASSERT(is_page_dirty_tracked(page));
          ASSERT(!is_page_clean(page));
          DEBUG_ASSERT(!page->is_loaned());

          // Start a run of committed pages if we are not tracking one yet.
          if (committed_len == 0) {
            committed_start = off;
          }
          // Add this page to the committed run and proceed to the next one.
          DEBUG_ASSERT(committed_start + committed_len == off);
          committed_len += PAGE_SIZE;
          return ZX_ERR_NEXT;
        },
        [&committed_start, &committed_len, &dirty_range_fn](uint64_t start, uint64_t end) {
          // If we were tracking a committed run, process it first.
          if (committed_len > 0) {
            // This gap should immediately follow the previous run of committed pages.
            DEBUG_ASSERT(committed_start + committed_len == start);
            zx_status_t status =
                dirty_range_fn(committed_start, committed_len, /*range_is_zero=*/false);
            // Only proceed to the next range if the return status indicates we can.
            if (status != ZX_ERR_NEXT) {
              return status;
            }
            // Reset committed_len for tracking another committed run later.
            committed_len = 0;
          }

          // Process this gap now. Indicate that this range is zero.
          return dirty_range_fn(start, end - start, /*range_is_zero=*/true);
        },
        start, end_offset);

    if (status != ZX_OK) {
      return status;
    }

    // Process any last remaining committed run.
    if (committed_len > 0) {
      status = dirty_range_fn(committed_start, committed_len, /*range_is_zero=*/false);
      if (status != ZX_ERR_STOP && status != ZX_ERR_NEXT) {
        return status;
      }
    }
  }

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  return ZX_OK;
}

zx_status_t VmCowPages::WritebackBeginLocked(uint64_t offset, uint64_t len, bool is_zero_range) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!is_source_preserving_page_content()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint64_t start_offset = offset;
  const uint64_t end_offset = offset + len;
  // We only need to consider transitioning pages if the caller has specified that this is not a
  // zero range. For a zero range, we cannot start cleaning any pages because the caller has
  // expressed intent to write back zeros in this range; any pages we clean might get evicted and
  // incorrectly supplied again as zero pages, leading to data loss.
  //
  // When querying dirty ranges, gaps beyond supply_zero_offset_ are indicated as dirty zero ranges.
  // So it's perfectly reasonable for the user pager to write back these zero ranges efficiently
  // without having to read the actual contents of the range, which would read zeroes anyway. There
  // can exist a race however, where the user pager has just discovered a dirty zero range, and
  // before it starts writing it out, an actual page gets dirtied in that range. Consider the
  // following example that demonstrates the race:
  //  1. The range [5, 10) is indicated as a dirty zero range when the user pager queries dirty
  //  ranges.
  //  2. A write comes in for page 7 and it is marked Dirty.
  //  3. The user pager prepares to write the range [5, 10) with WritebackBegin.
  //  4. Gaps as well as page 7 are marked AwaitingClean.
  //  5. The user pager still thinks that [5, 10) is zero and writes back zeroes for the range.
  //  6. The user pager does a WritebackEnd on [5, 10), and page 7 gets marked Clean.
  //  7. At some point in the future, page 7 gets evicted. The data on page 7 (which was prematurely
  //  marked Clean) is now lost.
  //
  // This race occurred because there was a mismatch between what the user pager and the kernel
  // think the contents of the range being written back are. The user pager intended to mark only
  // zero ranges (gaps) clean, not actual pages. The is_zero_range flag captures this intent, so
  // that the kernel does not incorrectly clean actual committed pages. Committed dirty pages will
  // be returned as actual dirty pages (not dirty zero ranges) on a subsequent call to query dirty
  // ranges, and can be cleaned then.
  if (!is_zero_range) {
    // All Dirty pages need to be marked AwaitingClean, irrespective of where they lie w.r.t.
    // supply_zero_offset_. If the VMO traps Dirty transitions, future writes need to be trapped in
    // order to generate DIRTY requests before marking the pages Dirty again. The userpager has
    // indicated that it is writing back contents as they exist at the time of this call, so new
    // writes altering those contents should be trapped and acknowledged by the userpager (the
    // filesystem might need to reserve additional space for the new writes).
    zx_status_t status = page_list_.ForEveryPageInRange(
        [this](const VmPageOrMarker* p, uint64_t off) {
          // VMOs with a page source should never have references.
          DEBUG_ASSERT(!p->IsReference());
          // If the page is pinned we have to leave it Dirty in case it is still being written to
          // via DMA. The VM system will be unaware of these writes, and so we choose to be
          // conservative here and might end up with pinned pages being left dirty for longer, until
          // a writeback is attempted after the unpin.
          if (p->IsPage() && p->Page()->object.pin_count > 0) {
            return ZX_ERR_NEXT;
          }
          // Transition pages from Dirty to AwaitingClean.
          if (p->IsPage() && is_page_dirty(p->Page())) {
            AssertHeld(lock_);
            UpdateDirtyStateLocked(p->Page(), off, DirtyState::AwaitingClean);
          }
          // We can only find actual pages beyond supply_zero_offset_ (no markers), and they will be
          // AwaitingClean, either from before this call or from having transitioned them to
          // AwaitingClean above. Pages beyond supply_zero_offset_ are un-Clean.
          AssertHeld(lock_);
          ASSERT(off < supply_zero_offset_ || (p->IsPage() && is_page_awaiting_clean(p->Page())));
          return ZX_ERR_NEXT;
        },
        start_offset, end_offset);
    // We don't expect a failure from the traversal.
    DEBUG_ASSERT(status == ZX_OK);
  }

  // If we were not tracking an awaiting clean zero range, see if we can start tracking one.
  if (awaiting_clean_zero_range_end_ == 0) {
    // We can only track an awaiting clean zero range if the start of the range, i.e.
    // supply_zero_offset_ lies completely within the specified range.
    if (supply_zero_offset_ >= start_offset && supply_zero_offset_ < end_offset) {
      awaiting_clean_zero_range_end_ = end_offset;
    }
  } else {
    DEBUG_ASSERT(supply_zero_offset_ < awaiting_clean_zero_range_end_);
    // If we were already tracking an awaiting clean zero range, see if we can extend it.
    if (awaiting_clean_zero_range_end_ >= start_offset &&
        awaiting_clean_zero_range_end_ < end_offset) {
      awaiting_clean_zero_range_end_ = end_offset;
    }
  }

  // Set any mappings for this range to read-only, so that a permission fault is triggered the next
  // time the page is written to in order for us to track it as dirty. This might cover more pages
  // than the Dirty pages found in the page list traversal above, but we choose to do this once for
  // the entire range instead of per page; pages in the AwaitingClean and Clean states will already
  // have their write permission removed, so this is a no-op for them.
  RangeChangeUpdateLocked(start_offset, end_offset - start_offset, RangeChangeOp::RemoveWrite);

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  return ZX_OK;
}

zx_status_t VmCowPages::WritebackEndLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!is_source_preserving_page_content()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint64_t start_offset = offset;
  const uint64_t end_offset = offset + len;

  // If writeback begins partway into the zero range described by supply_zero_offset_, return early.
  // We cannot clean any pages beyond supply_zero_offset_ unless we can also advance
  // supply_zero_offset_ to skip over the pages being cleaned; pages starting at supply_zero_offset_
  // are implicitly un-Clean.
  if (start_offset > supply_zero_offset_) {
    return ZX_OK;
  }

  // First consider the range before supply_zero_offset_.
  if (start_offset < supply_zero_offset_) {
    const uint64_t end = ktl::min(end_offset, supply_zero_offset_);
    zx_status_t status = page_list_.ForEveryPageInRange(
        [this](const VmPageOrMarker* p, uint64_t off) {
          // VMOs with a page source should never have references.
          DEBUG_ASSERT(!p->IsReference());
          // Transition pages from AwaitingClean to Clean.
          if (p->IsPage() && is_page_awaiting_clean(p->Page())) {
            AssertHeld(lock_);
            UpdateDirtyStateLocked(p->Page(), off, DirtyState::Clean);
          }
          return ZX_ERR_NEXT;
        },
        start_offset, end);
    // We don't expect a failure from the traversal.
    DEBUG_ASSERT(status == ZX_OK);
  }

  // No more work to be done if the range was entirely before supply_zero_offset.
  if (end_offset <= supply_zero_offset_) {
    return ZX_OK;
  }

  // If there is no AwaitingClean zero range, we cannot mark any more pages Clean.
  if (awaiting_clean_zero_range_end_ == 0) {
    return ZX_OK;
  }
  // Otherwise try to process the AwaitingClean zero range.

  DEBUG_ASSERT(supply_zero_offset_ < awaiting_clean_zero_range_end_);

  // End offset of the zero range that we can transition to Clean, and then advance
  // supply_zero_offset_ beyond.
  uint64_t zero_range_end = ktl::min(awaiting_clean_zero_range_end_, end_offset);

  DEBUG_ASSERT(supply_zero_offset_ < zero_range_end);

  // Clean any committed AwaitingClean pages in the range we will be advancing supply_zero_offset_
  // over. Note that we know we cannot fail beyond this point, so it is safe to clean pages as the
  // supply_zero_offset_ will be advanced over them. In other words, we are certain that we are not
  // violating the constraint that supply_zero_offset_ cannot be followed by Clean pages.
  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [this](const VmPageOrMarker* p, uint64_t off) {
        ASSERT(p->IsPage());
        vm_page_t* page = p->Page();
        ASSERT(is_page_dirty_tracked(page));
        ASSERT(!is_page_clean(page));
        if (is_page_awaiting_clean(page)) {
          // Mark the page Clean.
          AssertHeld(lock_);
          UpdateDirtyStateLocked(page, off, DirtyState::Clean);
        }
        return ZX_ERR_NEXT;
      },
      [](uint64_t start, uint64_t end) { return ZX_ERR_NEXT; }, supply_zero_offset_,
      zero_range_end);
  // We don't expect a failure from the traversal.
  DEBUG_ASSERT(status == ZX_OK);

  // Advance supply_zero_offset_ beyond the range we just cleaned. This will also update
  // awaiting_clean_zero_range_end_ if required.
  UpdateSupplyZeroOffsetLocked(zero_range_end);

  VMO_VALIDATION_ASSERT(DebugValidateSupplyZeroOffsetLocked());
  return ZX_OK;
}

const VmCowPages* VmCowPages::GetRootLocked() const {
  auto cow_pages = this;
  AssertHeld(cow_pages->lock_);
  while (cow_pages->parent_) {
    cow_pages = cow_pages->parent_.get();
    // We just checked that this is not null in the loop conditional.
    DEBUG_ASSERT(cow_pages);
  }
  DEBUG_ASSERT(cow_pages);
  return cow_pages;
}

fbl::RefPtr<PageSource> VmCowPages::GetRootPageSourceLocked() const {
  auto root = GetRootLocked();
  // The root will never be null. It will either point to a valid parent, or |this| if there's no
  // parent.
  DEBUG_ASSERT(root);
  return root->page_source_;
}

void VmCowPages::DetachSourceLocked() {
  DEBUG_ASSERT(page_source_);
  page_source_->Detach();

  // We stack-own loaned pages from UnmapAndRemovePagesLocked() to FreePagesLocked().
  __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

  list_node_t freed_list;
  list_initialize(&freed_list);

  // We would like to remove all committed pages so that all future page faults on this VMO and its
  // clones can fail in a deterministic manner. However, if the page source is preserving content
  // (is a userpager), we need to hold on to un-Clean (Dirty and AwaitingClean pages) so that they
  // can be written back by the page source. If the page source is not preserving content, its pages
  // will not be dirty tracked to begin with i.e. their dirty state will be Untracked, so we will
  // end up removing all pages.

  // We should only be removing pages from the root VMO.
  DEBUG_ASSERT(!parent_);

  // Even though we might end up removing only a subset of the pages, unmap them all at once as an
  // optimization. Only the userpager is expected to access (dirty) pages beyond this point, in
  // order to write back their contents, where the cost of the writeback is presumably much larger
  // than page faults to update hardware page table mappings for resident pages.
  RangeChangeUpdateLocked(0, size_, RangeChangeOp::Unmap);

  __UNINITIALIZED BatchPQRemove page_remover(&freed_list);

  // Remove all clean (or untracked) pages.
  // TODO(rashaeqbal): Pages that linger after this will be written back and marked clean at some
  // point, and will age through the pager-backed queues and eventually get evicted. We could
  // adopt an eager approach instead, and decommit those pages as soon as they get marked clean.
  // If we do that, we could also extend the eager approach to supply_pages, where pages get
  // decommitted on supply, i.e. the supply is a no-op.
  page_list_.RemovePages(
      [&page_remover](VmPageOrMarker* p, uint64_t off) {
        // A marker is a clean zero page. Replace it with an empty slot.
        if (p->IsMarker()) {
          *p = VmPageOrMarker::Empty();
          return ZX_ERR_NEXT;
        }
        // VMOs with a parge source cannot have references.
        DEBUG_ASSERT(p->IsPage());

        // We cannot remove the page if it is dirty-tracked but not clean.
        if (is_page_dirty_tracked(p->Page()) && !is_page_clean(p->Page())) {
          DEBUG_ASSERT(!p->Page()->is_loaned());
          return ZX_ERR_NEXT;
        }

        // This is a page that we're going to remove; we don't expect it to be pinned.
        DEBUG_ASSERT(p->Page()->object.pin_count == 0);

        page_remover.Push(p->ReleasePage());
        return ZX_ERR_NEXT;
      },
      0, size_);

  page_remover.Flush();
  FreePagesLocked(&freed_list, /*freeing_owned_pages=*/true);

  IncrementHierarchyGenerationCountLocked();
}

VmCowPages* VmCowPages::PagedParentOfSliceLocked(uint64_t* offset) {
  DEBUG_ASSERT(is_slice_locked());
  DEBUG_ASSERT(parent_);
  // Slices never have a slice parent, as there is no need to nest them.
  AssertHeld(parent_->lock_ref());
  DEBUG_ASSERT(!parent_->is_slice_locked());
  *offset = parent_offset_;
  return parent_.get();
}

void VmCowPages::RangeChangeUpdateFromParentLocked(const uint64_t offset, const uint64_t len,
                                                   RangeChangeList* list) {
  canary_.Assert();

  LTRACEF("offset %#" PRIx64 " len %#" PRIx64 " p_offset %#" PRIx64 " size_ %#" PRIx64 "\n", offset,
          len, parent_offset_, size_);

  // our parent is notifying that a range of theirs changed, see where it intersects
  // with our offset into the parent and pass it on
  uint64_t offset_new;
  uint64_t len_new;
  if (!GetIntersect(parent_offset_, size_, offset, len, &offset_new, &len_new)) {
    return;
  }

  // if they intersect with us, then by definition the new offset must be >= parent_offset_
  DEBUG_ASSERT(offset_new >= parent_offset_);

  // subtract our offset
  offset_new -= parent_offset_;

  // verify that it's still within range of us
  DEBUG_ASSERT(offset_new + len_new <= size_);

  LTRACEF("new offset %#" PRIx64 " new len %#" PRIx64 "\n", offset_new, len_new);

  // pass it on. to prevent unbounded recursion we package up our desired offset and len and add
  // ourselves to the list. UpdateRangeLocked will then get called on it later.
  // TODO: optimize by not passing on ranges that are completely covered by pages local to this vmo
  range_change_offset_ = offset_new;
  range_change_len_ = len_new;
  list->push_front(this);
}

void VmCowPages::RangeChangeUpdateListLocked(RangeChangeList* list, RangeChangeOp op) {
  while (!list->is_empty()) {
    VmCowPages* object = list->pop_front();
    AssertHeld(object->lock_);

    // Check if there is an associated backlink, and if so pass the operation over.
    if (object->paged_ref_) {
      AssertHeld(object->paged_ref_->lock_ref());
      object->paged_ref_->RangeChangeUpdateLocked(object->range_change_offset_,
                                                  object->range_change_len_, op);
    }

    // inform all our children this as well, so they can inform their mappings
    for (auto& child : object->children_list_) {
      AssertHeld(child.lock_);
      child.RangeChangeUpdateFromParentLocked(object->range_change_offset_,
                                              object->range_change_len_, list);
    }
  }
}

void VmCowPages::RangeChangeUpdateLocked(uint64_t offset, uint64_t len, RangeChangeOp op) {
  canary_.Assert();

  if (len == 0) {
    return;
  }

  RangeChangeList list;
  this->range_change_offset_ = offset;
  this->range_change_len_ = len;
  list.push_front(this);
  RangeChangeUpdateListLocked(&list, op);
}

// This method can be called on a VmCowPages whose refcount is 0, but whose VmCowPagesContainer
// refcount is still >= 1.  This can be running concurrently with VmCowPages::fbl_recycle(), but
// we know that ~VmCowPagesContainer won't run until after this call is over because the caller
// holds a refcount tally on the container.
bool VmCowPages::RemovePageForEviction(vm_page_t* page, uint64_t offset) {
  Guard<CriticalMutex> guard{&lock_};

  // Check this page is still a part of this VMO.
  const VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page) {
    return false;
  }

  // We shouldn't have been asked to evict a pinned page.
  ASSERT(page->object.pin_count == 0);

  // Ignore any hints, we were asked directly to evict.
  return RemovePageForEvictionLocked(page, offset, EvictionHintAction::Ignore);
}

bool VmCowPages::RemovePageForEvictionLocked(vm_page_t* page, uint64_t offset,
                                             EvictionHintAction hint_action) {
  // Without a page source to bring the page back in we cannot even think about eviction.
  if (!can_evict()) {
    return false;
  }

  // We can assume this page is in the VMO.
#if (DEBUG_ASSERT_IMPLEMENTED)
  {
    const VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
    DEBUG_ASSERT(page_or_marker);
    DEBUG_ASSERT(page_or_marker->IsPage());
    DEBUG_ASSERT(page_or_marker->Page() == page);
  }
#endif

  DEBUG_ASSERT(is_page_dirty_tracked(page));

  // We cannot evict the page unless it is clean. If the page is dirty, it will already have been
  // moved to the dirty page queue.
  if (!is_page_clean(page)) {
    DEBUG_ASSERT(!page->is_loaned());
    return false;
  }

  // Do not evict if the |always_need| hint is set, unless we are told to ignore the eviction hint.
  if (page->object.always_need == 1 && hint_action == EvictionHintAction::Follow) {
    DEBUG_ASSERT(!page->is_loaned());
    // We still need to move the page from the tail of the LRU page queue(s) so that the eviction
    // loop can make progress. Since this page is always needed, move it out of the way and into the
    // MRU queue. Do this here while we hold the lock, instead of at the callsite.
    //
    // TODO(rashaeqbal): Since we're essentially simulating an access here, this page may not
    // qualify for eviction if we do decide to override the hint soon after (i.e. if an OOM follows
    // shortly after). Investigate adding a separate queue once we have some more data around hints
    // usage. A possible approach might involve moving to a separate queue when we skip the page for
    // eviction. Pages move out of said queue when accessed, and continue aging as other pages.
    // Pages in the queue are considered for eviction pre-OOM, but ignored otherwise.
    UpdateOnAccessLocked(page, VMM_PF_FLAG_SW_FAULT);
    return false;
  }

  // Remove any mappings to this page before we remove it.
  RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);

  // Use RemovePage over just writing to page_or_marker so that the page list has the opportunity
  // to release any now empty intermediate nodes.
  vm_page_t* p = page_list_.RemoveContent(offset).ReleasePage();
  DEBUG_ASSERT(p == page);
  pmm_page_queues()->Remove(page);

  eviction_event_count_++;
  IncrementHierarchyGenerationCountLocked();
  VMO_VALIDATION_ASSERT(DebugValidatePageSplitsHierarchyLocked());
  VMO_FRUGAL_VALIDATION_ASSERT(DebugValidateVmoPageBorrowingLocked());
  // |page| is now owned by the caller.
  return true;
}

bool VmCowPages::ReclaimPage(vm_page_t* page, uint64_t offset, EvictionHintAction hint_action) {
  Guard<CriticalMutex> guard{&lock_};

  // Check this page is still a part of this VMO.
  const VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page) {
    return false;
  }

  // Pinned pages could be in use by DMA so we cannot safely reclaim them.
  if (page->object.pin_count != 0) {
    return false;
  }

  // See if we can reclaim by eviction.
  if (can_evict()) {
    return RemovePageForEvictionLocked(page, offset, hint_action);
  }
  // No other reclamation strategies, so to avoid this page remaining in a reclamation list we
  // simulate an access.
  UpdateOnAccessLocked(page, VMM_PF_FLAG_SW_FAULT);
  return false;
}

void VmCowPages::SwapPageLocked(uint64_t offset, vm_page_t* old_page, vm_page_t* new_page) {
  DEBUG_ASSERT(!old_page->object.pin_count);
  DEBUG_ASSERT(new_page->state() == vm_page_state::ALLOC);

  // unmap before removing old page
  RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);

  // Some of the fields initialized by this call get overwritten by CopyPageForReplacementLocked(),
  // and some don't (such as state()).
  InitializeVmPage(new_page);

  const VmPageOrMarker* p = page_list_.Lookup(offset);
  DEBUG_ASSERT(p);
  DEBUG_ASSERT(p->IsPage());

  CopyPageForReplacementLocked(new_page, old_page);

  // Add replacement page in place of old page.
  //
  // We could optimize this by doing what's needed to *p directly, but for now call this
  // common code.
  VmPageOrMarker new_vm_page = VmPageOrMarker::Page(new_page);
  VmPageOrMarker released_page;
  zx_status_t status = AddPageLocked(&new_vm_page, offset, CanOverwriteContent::NonZero,
                                     &released_page, /*do_range_update=*/false);
  // Absent bugs, AddPageLocked() can only return ZX_ERR_NO_MEMORY, but that failure can only occur
  // if page_list_ had to allocate.  Here, page_list_ hasn't yet had a chance to clean up any
  // internal structures, so AddPageLocked() didn't need to allocate, so we know that
  // AddPageLocked() will succeed.
  DEBUG_ASSERT(status == ZX_OK);
  // The page released was the old page.
  DEBUG_ASSERT(released_page.IsPage() && released_page.Page() == old_page);
  // Need to take the page out of |released_page| to avoid a [[nodiscard]] error. Since we just
  // checked that this matches the target page, which is now owned by the caller, this is not
  // leaking.
  [[maybe_unused]] vm_page_t* released = released_page.ReleasePage();
}

zx_status_t VmCowPages::ReplacePagesWithNonLoanedLocked(uint64_t offset, uint64_t len,
                                                        LazyPageRequest* page_request,
                                                        uint64_t* non_loaned_len) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));
  DEBUG_ASSERT(InRange(offset, len, size_));
  DEBUG_ASSERT(non_loaned_len);

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);

    // PagedParentOfSliceLocked will walk all of the way up the VMO hierarchy
    // until it hits a non-slice VMO.  This guarantees that we should only ever
    // recurse once instead of an unbound number of times.  DEBUG_ASSERT this so
    // that we don't actually end up with unbound recursion just in case the
    // property changes.
    DEBUG_ASSERT(!parent->is_slice_locked());

    return parent->ReplacePagesWithNonLoanedLocked(offset + parent_offset, len, page_request,
                                                   non_loaned_len);
  }

  *non_loaned_len = 0;
  return page_list_.ForEveryPageAndGapInRange(
      [page_request, non_loaned_len, this](const VmPageOrMarker* p, uint64_t off) {
        // We only expect committed pages in the specified range.
        if (p->IsMarker() || p->IsReference()) {
          return ZX_ERR_BAD_STATE;
        }
        vm_page_t* page = p->Page();
        // If the page is loaned, replace is with a non-loaned page.
        if (page->is_loaned()) {
          AssertHeld(lock_);
          // A loaned page could only have been clean.
          DEBUG_ASSERT(!is_page_dirty_tracked(page) || is_page_clean(page));
          DEBUG_ASSERT(page_request);
          zx_status_t status =
              ReplacePageLocked(page, off, /*with_loaned=*/false, &page, page_request);
          if (status == ZX_ERR_SHOULD_WAIT) {
            return status;
          }
          if (status != ZX_OK) {
            return ZX_ERR_BAD_STATE;
          }
        }
        DEBUG_ASSERT(!page->is_loaned());
        *non_loaned_len += PAGE_SIZE;
        return ZX_ERR_NEXT;
      },
      [](uint64_t start, uint64_t end) {
        // We only expect committed pages in the specified range.
        return ZX_ERR_BAD_STATE;
      },
      offset, offset + len);
}

zx_status_t VmCowPages::ReplacePageWithLoaned(vm_page_t* before_page, uint64_t offset) {
  Guard<CriticalMutex> guard{&lock_};
  return ReplacePageLocked(before_page, offset, true, nullptr, nullptr);
}

zx_status_t VmCowPages::ReplacePageLocked(vm_page_t* before_page, uint64_t offset, bool with_loaned,
                                          vm_page_t** after_page, LazyPageRequest* page_request) {
  // If not replacing with loaned it is required that a page_request be provided.
  DEBUG_ASSERT(with_loaned || page_request);

  const VmPageOrMarker* p = page_list_.Lookup(offset);
  if (!p) {
    return ZX_ERR_NOT_FOUND;
  }
  if (!p->IsPage()) {
    return ZX_ERR_NOT_FOUND;
  }
  vm_page_t* old_page = p->Page();
  if (old_page != before_page) {
    return ZX_ERR_NOT_FOUND;
  }
  DEBUG_ASSERT(old_page != vm_get_zero_page());
  if (old_page->object.pin_count != 0) {
    DEBUG_ASSERT(!old_page->is_loaned());
    return ZX_ERR_BAD_STATE;
  }
  if (old_page->object.always_need) {
    DEBUG_ASSERT(!old_page->is_loaned());
    return ZX_ERR_BAD_STATE;
  }
  uint32_t pmm_alloc_flags = pmm_alloc_flags_;
  if (with_loaned) {
    if (!can_borrow_locked()) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    if (is_page_dirty_tracked(old_page) && !is_page_clean(old_page)) {
      return ZX_ERR_BAD_STATE;
    }
    // Loaned page allocations will always precisely succeed or fail and the CAN_WAIT flag cannot be
    // combined and so we remove it if it exists.
    pmm_alloc_flags &= ~PMM_ALLOC_FLAG_CAN_WAIT;
    pmm_alloc_flags |= PMM_ALLOC_FLAG_CAN_BORROW | PMM_ALLOC_FLAG_MUST_BORROW;
  } else {
    pmm_alloc_flags &= ~PMM_ALLOC_FLAG_CAN_BORROW;
  }

  // We stack-own a loaned page from pmm_alloc_page() to SwapPageLocked() OR from SwapPageLocked()
  // until FreePageLocked().
  __UNINITIALIZED StackOwnedLoanedPagesInterval raii_interval;

  vm_page_t* new_page;
  zx_status_t status = pmm_alloc_page(pmm_alloc_flags, &new_page);
  if (status != ZX_OK) {
    if (status == ZX_ERR_SHOULD_WAIT) {
      DEBUG_ASSERT(page_request);
      return AnonymousPageRequester::Get().FillRequest(page_request->get());
    }
    return status;
  }
  SwapPageLocked(offset, old_page, new_page);
  pmm_page_queues()->Remove(old_page);
  FreePageLocked(old_page, /*freeing_owned_page=*/true);
  if (after_page) {
    *after_page = new_page;
  }
  return ZX_OK;
}

bool VmCowPages::DebugValidatePageSplitsHierarchyLocked() const {
  const VmCowPages* cur = this;
  AssertHeld(cur->lock_);
  const VmCowPages* parent_most = cur;
  do {
    if (!cur->DebugValidatePageSplitsLocked()) {
      return false;
    }
    cur = cur->parent_.get();
    if (cur) {
      parent_most = cur;
    }
  } while (cur);
  // Iterate whole hierarchy; the iteration order doesn't matter.  Since there are cases with
  // >2 children, in-order isn't well defined, so we choose pre-order, but post-order would also
  // be fine.
  const VmCowPages* prev = nullptr;
  cur = parent_most;
  while (cur) {
    uint32_t children = cur->children_list_len_;
    if (!prev || prev == cur->parent_.get()) {
      // Visit cur
      if (!cur->DebugValidateBacklinksLocked()) {
        dprintf(INFO, "cur: %p this: %p\n", cur, this);
        return false;
      }

      if (!children) {
        // no children; move to parent (or nullptr)
        prev = cur;
        cur = cur->parent_.get();
        continue;
      } else {
        // move to first child
        prev = cur;
        cur = &cur->children_list_.front();
        continue;
      }
    }
    // At this point we know we came up from a child, not down from the parent.
    DEBUG_ASSERT(prev && prev != cur->parent_.get());
    // The children are linked together, so we can move from one child to the next.

    auto iterator = cur->children_list_.make_iterator(*prev);
    ++iterator;
    if (iterator == cur->children_list_.end()) {
      // no more children; move back to parent
      prev = cur;
      cur = cur->parent_.get();
      continue;
    }

    // descend to next child
    prev = cur;
    cur = &(*iterator);
    DEBUG_ASSERT(cur);
  }
  return true;
}

bool VmCowPages::DebugValidatePageSplitsLocked() const {
  canary_.Assert();

  // Assume this is valid until we prove otherwise.
  bool valid = true;
  page_list_.ForEveryPage([this, &valid](const VmPageOrMarker* page, uint64_t offset) {
    if (!page->IsPageOrRef()) {
      return ZX_ERR_NEXT;
    }
    AssertHeld(this->lock_);

    // All pages in non-hidden VMOs should not be split, as this is a meaningless thing to talk
    // about and indicates a book keeping error somewhere else.
    if (!this->is_hidden_locked()) {
      if (page->PageOrRefLeftSplit() || page->PageOrRefRightSplit()) {
        if (page->IsPage()) {
          printf("Found split page %p (off %p) in non-hidden node %p\n", page->Page(),
                 (void*)offset, this);
        } else {
          printf("Found split reference off %p in non-hidden node%p\n", (void*)offset, this);
        }
        this->DumpLocked(1, true);
        valid = false;
        return ZX_ERR_STOP;
      }
      // Nothing else to test for non-hidden VMOs.
      return ZX_ERR_NEXT;
    }

    // We found a page in the hidden VMO, if it has been forked in either direction then we
    // expect that if we search down that path we will find that the forked page and that no
    // descendant can 'see' back to this page.
    const VmCowPages* expected = nullptr;
    if (page->PageOrRefLeftSplit()) {
      expected = &left_child_locked();
    } else if (page->PageOrRefRightSplit()) {
      expected = &right_child_locked();
    } else {
      return ZX_ERR_NEXT;
    }

    // We know this must be true as this is a hidden vmo and so left_child_locked and
    // right_child_locked will never have returned null.
    DEBUG_ASSERT(expected);

    // No leaf VMO in expected should be able to 'see' this page and potentially re-fork it. To
    // validate this we need to walk the entire sub tree.
    const VmCowPages* cur = expected;
    uint64_t off = offset;
    // We start with cur being an immediate child of 'this', so we can preform subtree traversal
    // until we end up back in 'this'.
    while (cur != this) {
      AssertHeld(cur->lock_);
      // Check that we can see this page in the parent. Importantly this first checks if
      // |off < cur->parent_offset_| allowing us to safely perform that subtraction from then on.
      if (off < cur->parent_offset_ || off - cur->parent_offset_ < cur->parent_start_limit_ ||
          off - cur->parent_offset_ >= cur->parent_limit_) {
        // This blank case is used to capture the scenario where current does not see the target
        // offset in the parent, in which case there is no point traversing into the children.
      } else if (cur->is_hidden_locked()) {
        // A hidden VMO *may* have the page, but not necessarily if both children forked it out.
        const VmPageOrMarker* l = cur->page_list_.Lookup(off - cur->parent_offset_);
        if (!l || l->IsEmpty()) {
          // Page not found, we need to recurse down into our children.
          off -= cur->parent_offset_;
          cur = &cur->left_child_locked();
          continue;
        }
      } else {
        // We already checked in the first 'if' branch that this offset was visible, and so this
        // leaf VMO *must* have a page or marker to prevent it 'seeing' the already forked original.
        const VmPageOrMarker* l = cur->page_list_.Lookup(off - cur->parent_offset_);
        if (!l || l->IsEmpty()) {
          if (page->IsPage()) {
            printf("Failed to find fork of page %p (off %p) from %p in leaf node %p (off %p)\n",
                   page->Page(), (void*)offset, this, cur, (void*)(off - cur->parent_offset_));
          } else {
            printf("Failed to find fork of reference (off %p) from %p in leaf node %p (off %p)\n",
                   (void*)offset, this, cur, (void*)(off - cur->parent_offset_));
          }
          cur->DumpLocked(1, true);
          this->DumpLocked(1, true);
          valid = false;
          return ZX_ERR_STOP;
        }
      }

      // Find our next node by walking up until we see we have come from a left path, then go right.
      do {
        VmCowPages* next = cur->parent_.get();
        AssertHeld(next->lock_);
        off += next->parent_offset_;
        if (next == this) {
          cur = next;
          break;
        }

        // If we came from the left, go back down on the right, otherwise just keep going up.
        if (cur == &next->left_child_locked()) {
          off -= next->parent_offset_;
          cur = &next->right_child_locked();
          break;
        }
        cur = next;
      } while (1);
    }

    // The inverse case must also exist where the side that hasn't forked it must still be able to
    // see it. It can either be seen by a leaf vmo that does not have a page, or a hidden vmo that
    // has partial_cow_release_ set.
    // No leaf VMO in expected should be able to 'see' this page and potentially re-fork it. To
    // validate this we need to walk the entire sub tree.
    if (page->PageOrRefLeftSplit()) {
      cur = &right_child_locked();
    } else if (page->PageOrRefRightSplit()) {
      cur = &left_child_locked();
    } else {
      return ZX_ERR_NEXT;
    }
    off = offset;
    // Initially we haven't seen the page, unless this VMO itself has done a partial cow release, in
    // which case we ourselves can see it. Logic is structured this way to avoid indenting this
    // whole code block in an if, whilst preserving the ability to add future checks below.
    bool seen = partial_cow_release_;
    // We start with cur being an immediate child of 'this', so we can preform subtree traversal
    // until we end up back in 'this'.
    while (cur != this && !seen) {
      AssertHeld(cur->lock_);
      // Check that we can see this page in the parent. Importantly this first checks if
      // |off < cur->parent_offset_| allowing us to safely perform that subtraction from then on.
      if (off < cur->parent_offset_ || off - cur->parent_offset_ < cur->parent_start_limit_ ||
          off - cur->parent_offset_ >= cur->parent_limit_) {
        // This blank case is used to capture the scenario where current does not see the target
        // offset in the parent, in which case there is no point traversing into the children.
      } else if (cur->is_hidden_locked()) {
        // A hidden VMO can see the page if it performed a partial cow release.
        if (cur->partial_cow_release_) {
          seen = true;
          break;
        }
        // Otherwise recurse into the children.
        off -= cur->parent_offset_;
        cur = &cur->left_child_locked();
        continue;
      } else {
        // We already checked in the first 'if' branch that this offset was visible, and so if this
        // leaf has no committed page then it is able to see it.
        const VmPageOrMarker* l = cur->page_list_.Lookup(off - cur->parent_offset_);
        if (!l || l->IsEmpty()) {
          seen = true;
          break;
        }
      }
      // Find our next node by walking up until we see we have come from a left path, then go right.
      do {
        VmCowPages* next = cur->parent_.get();
        AssertHeld(next->lock_);
        off += next->parent_offset_;
        if (next == this) {
          cur = next;
          break;
        }

        // If we came from the left, go back down on the right, otherwise just keep going up.
        if (cur == &next->left_child_locked()) {
          off -= next->parent_offset_;
          cur = &next->right_child_locked();
          break;
        }
        cur = next;
      } while (1);
    }
    if (!seen) {
      if (page->IsPage()) {
        printf(
            "Failed to find any child who could fork the remaining split page %p (off %p) in node "
            "%p\n",
            page->Page(), (void*)offset, this);
      } else {
        printf(
            "Failed to find any child who could fork the remaining split reference (off %p) in "
            "node "
            "%p\n",
            (void*)offset, this);
      }
      this->DumpLocked(1, true);
      printf("Left:\n");
      left_child_locked().DumpLocked(1, true);
      printf("Right:\n");
      right_child_locked().DumpLocked(1, true);
      valid = false;
      return ZX_ERR_STOP;
    }
    return ZX_ERR_NEXT;
  });

  return valid;
}

bool VmCowPages::DebugValidateBacklinksLocked() const {
  canary_.Assert();
  bool result = true;
  page_list_.ForEveryPage([this, &result](const auto* p, uint64_t offset) {
    // Markers and references don't have backlinks.
    if (p->IsReference() || p->IsMarker()) {
      return ZX_ERR_NEXT;
    }
    vm_page_t* page = p->Page();
    vm_page_state state = page->state();
    if (state != vm_page_state::OBJECT) {
      dprintf(INFO, "unexpected page state: %u\n", static_cast<uint32_t>(state));
      result = false;
      return ZX_ERR_STOP;
    }
    const VmCowPages* object = reinterpret_cast<VmCowPages*>(page->object.get_object());
    if (!object) {
      dprintf(INFO, "missing object\n");
      result = false;
      return ZX_ERR_STOP;
    }
    if (object != this) {
      dprintf(INFO, "incorrect object - object: %p this: %p\n", object, this);
      result = false;
      return ZX_ERR_STOP;
    }
    uint64_t page_offset = page->object.get_page_offset();
    if (page_offset != offset) {
      dprintf(INFO, "incorrect offset - page_offset: %" PRIx64 " offset: %" PRIx64 "\n",
              page_offset, offset);
      result = false;
      return ZX_ERR_STOP;
    }
    return ZX_ERR_NEXT;
  });
  return result;
}

bool VmCowPages::DebugValidateVmoPageBorrowingLocked() const {
  DEBUG_ASSERT(this);
  // Skip checking larger VMOs to avoid slowing things down too much, since the things being
  // verified will typically assert from incorrect behavior on smaller VMOs (and we can always
  // remove this filter if we suspect otherwise).
  if (size_ >= 2 * 1024 * 1024) {
    return true;
  }
  canary_.Assert();
  bool result = true;
  page_list_.ForEveryPage([this, &result](const auto* p, uint64_t offset) {
    AssertHeld(lock_);
    if (!p->IsPage()) {
      // If we don't have a page, this is either a marker or reference, both of which are not
      // allowed with contiguous VMOs.
      DEBUG_ASSERT(!direct_source_supplies_zero_pages());
      return ZX_ERR_NEXT;
    }
    vm_page_t* page = p->Page();
    if (page->is_loaned()) {
      if (!can_borrow_locked()) {
        dprintf(INFO, "!can_borrow_locked() but page is loaned?? - offset: 0x%" PRIx64 "\n",
                offset);
        result = false;
        return ZX_ERR_STOP;
      }
      if (page->object.pin_count) {
        dprintf(INFO, "pinned page is loaned?? - offset: 0x%" PRIx64 "\n", offset);
        result = false;
        return ZX_ERR_STOP;
      }
      if (page->object.always_need) {
        dprintf(INFO, "always_need page is loaned?? - offset: 0x%" PRIx64 "\n", offset);
        result = false;
        return ZX_ERR_STOP;
      }
      if (is_page_dirty_tracked(page) && !is_page_clean(page)) {
        dprintf(INFO, "!clean page is loaned?? - offset: 0x%" PRIx64 "\n", offset);
        result = false;
        return ZX_ERR_STOP;
      }
    }
    return ZX_ERR_NEXT;
  });
  if (!result) {
    dprintf(INFO, "DebugValidateVmoPageBorrowingLocked() failing - slice: %d\n", is_slice_locked());
  }
  return result;
}

bool VmCowPages::DebugValidateSupplyZeroOffsetLocked() const {
  if (supply_zero_offset_ == UINT64_MAX) {
    return true;
  }
  if (!is_source_preserving_page_content()) {
    dprintf(INFO, "supply_zero_offset_=%zu for non pager backed vmo\n", supply_zero_offset_);
    return false;
  }
  if (supply_zero_offset_ > size_) {
    dprintf(INFO, "supply_zero_offset_=%zu larger than size=%zu\n", supply_zero_offset_, size_);
    return false;
  }
  if (awaiting_clean_zero_range_end_ != 0 &&
      supply_zero_offset_ >= awaiting_clean_zero_range_end_) {
    dprintf(INFO, "supply_zero_offset_=%zu larger than awaiting_clean_zero_range_end_=%zu\n",
            supply_zero_offset_, awaiting_clean_zero_range_end_);
    return false;
  }

  zx_status_t status = page_list_.ForEveryPageInRange(
      [supply_zero_offset = supply_zero_offset_](const VmPageOrMarker* p, uint64_t off) {
        if (p->IsMarker()) {
          dprintf(INFO, "found marker at offset %zu (supply_zero_offset_=%zu)\n", off,
                  supply_zero_offset);
          return ZX_ERR_BAD_STATE;
        }
        if (p->IsReference()) {
          dprintf(INFO, "found reference at offset %zu (supply_zero_offset_=%zu)\n", off,
                  supply_zero_offset);
          return ZX_ERR_BAD_STATE;
        }
        vm_page_t* page = p->Page();
        if (!is_page_dirty_tracked(page)) {
          dprintf(INFO, "page at offset %zu not dirty tracked (supply_zero_offset_=%zu)\n", off,
                  supply_zero_offset);
          return ZX_ERR_BAD_STATE;
        }
        if (is_page_clean(page)) {
          dprintf(INFO, "page at offset %zu clean (supply_zero_offset_=%zu)\n", off,
                  supply_zero_offset);
          return ZX_ERR_BAD_STATE;
        }
        if (page->is_loaned()) {
          dprintf(INFO, "page at offset %zu loaned (supply_zero_offset_=%zu)\n", off,
                  supply_zero_offset);
          return ZX_ERR_BAD_STATE;
        }
        return ZX_ERR_NEXT;
      },
      supply_zero_offset_, size_);

  return status == ZX_OK;
}

bool VmCowPages::IsLockRangeValidLocked(uint64_t offset, uint64_t len) const {
  return offset == 0 && len == size_locked();
}

zx_status_t VmCowPages::LockRangeLocked(uint64_t offset, uint64_t len,
                                        zx_vmo_lock_state_t* lock_state_out) {
  canary_.Assert();

  AssertHeld(lock_);
  if (!IsLockRangeValidLocked(offset, len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!lock_state_out) {
    return ZX_ERR_INVALID_ARGS;
  }
  lock_state_out->offset = offset;
  lock_state_out->size = len;

  if (discardable_state_ == DiscardableState::kDiscarded) {
    DEBUG_ASSERT(lock_count_ == 0);
    lock_state_out->discarded_offset = 0;
    lock_state_out->discarded_size = size_locked();
  } else {
    lock_state_out->discarded_offset = 0;
    lock_state_out->discarded_size = 0;
  }

  if (lock_count_ == 0) {
    // Lock count transition from 0 -> 1. Change state to unreclaimable.
    UpdateDiscardableStateLocked(DiscardableState::kUnreclaimable);
  }
  ++lock_count_;

  return ZX_OK;
}

zx_status_t VmCowPages::TryLockRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  AssertHeld(lock_);
  if (!IsLockRangeValidLocked(offset, len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (discardable_state_ == DiscardableState::kDiscarded) {
    return ZX_ERR_UNAVAILABLE;
  }

  if (lock_count_ == 0) {
    // Lock count transition from 0 -> 1. Change state to unreclaimable.
    UpdateDiscardableStateLocked(DiscardableState::kUnreclaimable);
  }
  ++lock_count_;

  return ZX_OK;
}

zx_status_t VmCowPages::UnlockRangeLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  AssertHeld(lock_);
  if (!IsLockRangeValidLocked(offset, len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (lock_count_ == 0) {
    return ZX_ERR_BAD_STATE;
  }

  if (lock_count_ == 1) {
    // Lock count transition from 1 -> 0. Change state to reclaimable.
    UpdateDiscardableStateLocked(DiscardableState::kReclaimable);
  }
  --lock_count_;

  return ZX_OK;
}

void VmCowPages::UpdateDiscardableStateLocked(DiscardableState state) {
  Guard<CriticalMutex> guard{DiscardableVmosLock::Get()};

  DEBUG_ASSERT(state != DiscardableState::kUnset);

  if (state == discardable_state_) {
    return;
  }

  switch (state) {
    case DiscardableState::kReclaimable:
      // The only valid transition into reclaimable is from unreclaimable (lock count 1 -> 0).
      DEBUG_ASSERT(discardable_state_ == DiscardableState::kUnreclaimable);
      DEBUG_ASSERT(lock_count_ == 1);

      // Update the last unlock timestamp.
      last_unlock_timestamp_ = current_time();

      // Move to reclaim candidates list.
      MoveToReclaimCandidatesListLocked();

      break;
    case DiscardableState::kUnreclaimable:
      // The vmo could be reclaimable OR discarded OR not on any list yet. In any case, the lock
      // count should be 0.
      DEBUG_ASSERT(lock_count_ == 0);
      DEBUG_ASSERT(discardable_state_ != DiscardableState::kUnreclaimable);

      if (discardable_state_ == DiscardableState::kDiscarded) {
        // Should already be on the non reclaim candidates list.
        DEBUG_ASSERT(discardable_non_reclaim_candidates_.find_if([this](auto& cow) -> bool {
          return &cow == this;
        }) != discardable_non_reclaim_candidates_.end());
      } else {
        // Move to non reclaim candidates list.
        MoveToNonReclaimCandidatesListLocked(discardable_state_ == DiscardableState::kUnset);
      }

      break;
    case DiscardableState::kDiscarded:
      // The only valid transition into discarded is from reclaimable (lock count is 0).
      DEBUG_ASSERT(discardable_state_ == DiscardableState::kReclaimable);
      DEBUG_ASSERT(lock_count_ == 0);

      // Move from reclaim candidates to non reclaim candidates list.
      MoveToNonReclaimCandidatesListLocked();

      break;
    default:
      break;
  }

  // Update the state.
  discardable_state_ = state;
}

void VmCowPages::RemoveFromDiscardableListLocked() {
  Guard<CriticalMutex> guard{DiscardableVmosLock::Get()};
  if (discardable_state_ == DiscardableState::kUnset) {
    return;
  }

  DEBUG_ASSERT(fbl::InContainer<internal::DiscardableListTag>(*this));

  Cursor::AdvanceCursors(discardable_vmos_cursors_, this);

  if (discardable_state_ == DiscardableState::kReclaimable) {
    discardable_reclaim_candidates_.erase(*this);
  } else {
    discardable_non_reclaim_candidates_.erase(*this);
  }

  discardable_state_ = DiscardableState::kUnset;
}

void VmCowPages::MoveToReclaimCandidatesListLocked() {
  DEBUG_ASSERT(fbl::InContainer<internal::DiscardableListTag>(*this));

  Cursor::AdvanceCursors(discardable_vmos_cursors_, this);
  discardable_non_reclaim_candidates_.erase(*this);

  discardable_reclaim_candidates_.push_back(this);
}

void VmCowPages::MoveToNonReclaimCandidatesListLocked(bool new_candidate) {
  if (new_candidate) {
    DEBUG_ASSERT(!fbl::InContainer<internal::DiscardableListTag>(*this));
  } else {
    DEBUG_ASSERT(fbl::InContainer<internal::DiscardableListTag>(*this));
    Cursor::AdvanceCursors(discardable_vmos_cursors_, this);
    discardable_reclaim_candidates_.erase(*this);
  }

  discardable_non_reclaim_candidates_.push_back(this);
}

bool VmCowPages::DebugIsInDiscardableListLocked(bool reclaim_candidate) const {
  AssertHeld(lock_);
  Guard<CriticalMutex> guard{DiscardableVmosLock::Get()};

  // Not on any list yet. Nothing else to verify.
  if (discardable_state_ == DiscardableState::kUnset) {
    return false;
  }

  DEBUG_ASSERT(fbl::InContainer<internal::DiscardableListTag>(*this));

  auto iter_c =
      discardable_reclaim_candidates_.find_if([this](auto& cow) -> bool { return &cow == this; });
  auto iter_nc = discardable_non_reclaim_candidates_.find_if(
      [this](auto& cow) -> bool { return &cow == this; });

  if (reclaim_candidate) {
    // Verify that the vmo is in the |discardable_reclaim_candidates_| list and NOT in the
    // |discardable_non_reclaim_candidates_| list.
    if (iter_c != discardable_reclaim_candidates_.end() &&
        iter_nc == discardable_non_reclaim_candidates_.end()) {
      return true;
    }
  } else {
    // Verify that the vmo is in the |discardable_non_reclaim_candidates_| list and NOT in the
    // |discardable_reclaim_candidates_| list.
    if (iter_nc != discardable_non_reclaim_candidates_.end() &&
        iter_c == discardable_reclaim_candidates_.end()) {
      return true;
    }
  }

  return false;
}

uint64_t VmCowPages::DebugGetPageCountLocked() const {
  uint64_t page_count = 0;
  zx_status_t status = page_list_.ForEveryPage([&page_count](auto* p, uint64_t offset) {
    if (!p->IsPageOrRef()) {
      return ZX_ERR_NEXT;
    }
    ++page_count;
    return ZX_ERR_NEXT;
  });
  // We never stop early in lambda above.
  DEBUG_ASSERT(status == ZX_OK);
  return page_count;
}

bool VmCowPages::DebugIsReclaimable() const {
  Guard<CriticalMutex> guard{&lock_};
  if (discardable_state_ != DiscardableState::kReclaimable) {
    return false;
  }
  return DebugIsInDiscardableListLocked(/*reclaim_candidate=*/true);
}

bool VmCowPages::DebugIsUnreclaimable() const {
  Guard<CriticalMutex> guard{&lock_};
  if (discardable_state_ != DiscardableState::kUnreclaimable) {
    return false;
  }
  return DebugIsInDiscardableListLocked(/*reclaim_candidate=*/false);
}

bool VmCowPages::DebugIsDiscarded() const {
  Guard<CriticalMutex> guard{&lock_};
  if (discardable_state_ != DiscardableState::kDiscarded) {
    return false;
  }
  return DebugIsInDiscardableListLocked(/*reclaim_candidate=*/false);
}

bool VmCowPages::DebugIsPage(uint64_t offset) const {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  Guard<CriticalMutex> guard{&lock_};
  const VmPageOrMarker* p = page_list_.Lookup(offset);
  return p && p->IsPage();
}

bool VmCowPages::DebugIsMarker(uint64_t offset) const {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  Guard<CriticalMutex> guard{&lock_};
  const VmPageOrMarker* p = page_list_.Lookup(offset);
  return p && p->IsMarker();
}

bool VmCowPages::DebugIsEmpty(uint64_t offset) const {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  Guard<CriticalMutex> guard{&lock_};
  const VmPageOrMarker* p = page_list_.Lookup(offset);
  return !p || p->IsEmpty();
}

vm_page_t* VmCowPages::DebugGetPage(uint64_t offset) const {
  Guard<CriticalMutex> guard{&lock_};
  return DebugGetPageLocked(offset);
}

vm_page_t* VmCowPages::DebugGetPageLocked(uint64_t offset) const {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  const VmPageOrMarker* p = page_list_.Lookup(offset);
  if (p && p->IsPage()) {
    return p->Page();
  }
  return nullptr;
}

uint64_t VmCowPages::DebugGetSupplyZeroOffset() const {
  Guard<CriticalMutex> guard{&lock_};
  return supply_zero_offset_;
}

VmCowPages::DiscardablePageCounts VmCowPages::GetDiscardablePageCounts() const {
  DiscardablePageCounts counts = {};

  Guard<CriticalMutex> guard{&lock_};
  if (discardable_state_ == DiscardableState::kUnset) {
    return counts;
  }

  uint64_t pages = 0;
  page_list_.ForEveryPage([&pages](const auto* p, uint64_t) {
    // TODO(fxbug.dev/60238) Figure out attribution between pages and references.
    if (p->IsPageOrRef()) {
      ++pages;
    }
    return ZX_ERR_NEXT;
  });

  switch (discardable_state_) {
    case DiscardableState::kReclaimable:
      counts.unlocked = pages;
      break;
    case DiscardableState::kUnreclaimable:
      counts.locked = pages;
      break;
    case DiscardableState::kDiscarded:
      DEBUG_ASSERT(pages == 0);
      break;
    default:
      break;
  }

  return counts;
}

VmCowPages::DiscardablePageCounts VmCowPages::DebugDiscardablePageCounts() {
  DiscardablePageCounts total_counts = {};
  Guard<CriticalMutex> guard{DiscardableVmosLock::Get()};

  // The union of the two lists should give us a list of all discardable vmos.
  DiscardableList* lists_to_process[] = {&discardable_reclaim_candidates_,
                                         &discardable_non_reclaim_candidates_};

  for (auto list : lists_to_process) {
    Cursor cursor(DiscardableVmosLock::Get(), *list, discardable_vmos_cursors_);
    AssertHeld(cursor.lock_ref());

    VmCowPages* cow;
    while ((cow = cursor.Next())) {
      fbl::RefPtr<VmCowPages> cow_ref = fbl::MakeRefPtrUpgradeFromRaw(cow, guard);
      if (cow_ref) {
        // Get page counts for each vmo outside of the |DiscardableVmosLock|, since
        // GetDiscardablePageCounts() will acquire the VmCowPages lock. Holding the
        // |DiscardableVmosLock| while acquiring the VmCowPages lock will violate lock ordering
        // constraints between the two.
        //
        // Since we upgraded the raw pointer to a RefPtr under the |DiscardableVmosLock|, we know
        // that the object is valid. We will call Next() on our cursor after re-acquiring the
        // |DiscardableVmosLock| to safely iterate to the next element on the list.
        guard.CallUnlocked([&total_counts, cow_ref = ktl::move(cow_ref)]() mutable {
          DiscardablePageCounts counts = cow_ref->GetDiscardablePageCounts();
          total_counts.locked += counts.locked;
          total_counts.unlocked += counts.unlocked;

          // Explicitly reset the RefPtr to force any destructor to run right now and not in the
          // cleanup of the lambda, which might happen after the |DiscardableVmosLock| has been
          // re-acquired.
          cow_ref.reset();
        });
      }
    }
  }

  return total_counts;
}

uint64_t VmCowPages::DiscardPages(zx_duration_t min_duration_since_reclaimable,
                                  list_node_t* freed_list) {
  canary_.Assert();

  Guard<CriticalMutex> guard{&lock_};

  // Either this vmo is not discardable, or we've raced with a lock operation. Bail without doing
  // anything. If this was a discardable vmo, the lock operation will have already moved it to the
  // unreclaimable list.
  if (discardable_state_ != DiscardableState::kReclaimable) {
    return 0;
  }

  // If the vmo was unlocked less than |min_duration_since_reclaimable| in the past, do not discard
  // from it yet.
  if (zx_time_sub_time(current_time(), last_unlock_timestamp_) < min_duration_since_reclaimable) {
    return 0;
  }

  // We've verified that the state is |kReclaimable|, so the lock count should be zero.
  DEBUG_ASSERT(lock_count_ == 0);

  uint64_t pages_freed = 0;

  // Remove all pages.
  zx_status_t status = UnmapAndRemovePagesLocked(0, size_, freed_list, &pages_freed);

  if (status != ZX_OK) {
    printf("Failed to remove pages from discardable vmo %p: %d\n", this, status);
    return pages_freed;
  }

  IncrementHierarchyGenerationCountLocked();

  // Update state to discarded.
  UpdateDiscardableStateLocked(DiscardableState::kDiscarded);

  return pages_freed;
}

uint64_t VmCowPages::ReclaimPagesFromDiscardableVmos(uint64_t target_pages,
                                                     zx_duration_t min_duration_since_reclaimable,
                                                     list_node_t* freed_list) {
  uint64_t total_pages_discarded = 0;
  Guard<CriticalMutex> guard{DiscardableVmosLock::Get()};

  Cursor cursor(DiscardableVmosLock::Get(), discardable_reclaim_candidates_,
                discardable_vmos_cursors_);
  AssertHeld(cursor.lock_ref());

  while (total_pages_discarded < target_pages) {
    VmCowPages* cow = cursor.Next();
    // No vmos to reclaim pages from.
    if (cow == nullptr) {
      break;
    }

    fbl::RefPtr<VmCowPages> cow_ref = fbl::MakeRefPtrUpgradeFromRaw(cow, guard);
    if (cow_ref) {
      // We obtained the RefPtr above under the |DiscardableVmosLock|, so we know the object is
      // valid. We could not have raced with destruction, since the object is removed from the
      // discardable list on the destruction path, which requires the |DiscardableVmosLock|.
      //
      // DiscardPages() will acquire the VmCowPages |lock_|, so it needs to be called outside of
      // the |DiscardableVmosLock|. This preserves lock ordering constraints between the two locks
      // - |DiscardableVmosLock| can be acquired while holding the VmCowPages |lock_|, but not the
      // other way around.
      guard.CallUnlocked([&total_pages_discarded, min_duration_since_reclaimable, &freed_list,
                          cow_ref = ktl::move(cow_ref)]() mutable {
        total_pages_discarded += cow_ref->DiscardPages(min_duration_since_reclaimable, freed_list);

        // Explicitly reset the RefPtr to force any destructor to run right now and not in the
        // cleanup of the lambda, which might happen after the |DiscardableVmosLock| has been
        // re-acquired.
        cow_ref.reset();
      });
    }
  }
  return total_pages_discarded;
}

void VmCowPages::CopyPageForReplacementLocked(vm_page_t* dst_page, vm_page_t* src_page) {
  DEBUG_ASSERT(!src_page->object.pin_count);
  void* src = paddr_to_physmap(src_page->paddr());
  DEBUG_ASSERT(src);
  void* dst = paddr_to_physmap(dst_page->paddr());
  DEBUG_ASSERT(dst);
  memcpy(dst, src, PAGE_SIZE);
  if (paged_ref_) {
    AssertHeld(paged_ref_->lock_ref());
    if (paged_ref_->GetMappingCachePolicyLocked() != ARCH_MMU_FLAG_CACHED) {
      arch_clean_invalidate_cache_range((vaddr_t)dst, PAGE_SIZE);
    }
  }
  dst_page->object.cow_left_split = src_page->object.cow_left_split;
  dst_page->object.cow_right_split = src_page->object.cow_right_split;
  dst_page->object.always_need = src_page->object.always_need;
  DEBUG_ASSERT(!dst_page->object.always_need || (!dst_page->is_loaned() && !src_page->is_loaned()));
  dst_page->object.dirty_state = src_page->object.dirty_state;
}

VmCowPagesContainer* VmCowPages::raw_container() {
  DEBUG_ASSERT(container_);
  return container_.get();
}

// This takes all the constructor parameters including the VmCowPagesContainer, which avoids any
// possiblity of allocation failure.
template <class... Args>
fbl::RefPtr<VmCowPages> VmCowPages::NewVmCowPages(
    ktl::unique_ptr<VmCowPagesContainer> cow_container, Args&&... args) {
  VmCowPagesContainer* raw_cow_container = cow_container.get();
  cow_container->EmplaceCow(ktl::move(cow_container), ktl::forward<Args>(args)...);
  auto cow = fbl::AdoptRef<VmCowPages>(&raw_cow_container->cow());
  return cow;
}

// This takes all the constructor parameters except for the VmCowPagesContainer which is allocated.
// The AllocChecker will reflect whether allocation was successful.
template <class... Args>
fbl::RefPtr<VmCowPages> VmCowPages::NewVmCowPages(fbl::AllocChecker* ac, Args&&... args) {
  auto cow_container = ktl::make_unique<VmCowPagesContainer>(ac);
  // Don't check via the AllocChecker so that the caller is still forced to check via the
  // AllocChecker.
  if (!cow_container) {
    return nullptr;
  }
  return NewVmCowPages(ktl::move(cow_container), ktl::forward<Args>(args)...);
}

VmCowPagesContainer::~VmCowPagesContainer() {
  if (is_cow_present_) {
    reinterpret_cast<VmCowPages*>(&cow_space_)->~VmCowPages();
    is_cow_present_ = false;
  }
}

bool VmCowPagesContainer::RemovePageForEviction(vm_page_t* page, uint64_t offset) {
  // While the caller must have a ref on VmCowPagesContainer, the caller doesn't need to have a ref
  // on VmCowPages, for RemovePageForEviction() in particular.
  DEBUG_ASSERT(ref_count_debug() >= 1);
  return cow().RemovePageForEviction(page, offset);
}

zx_status_t VmCowPagesContainer::ReplacePage(vm_page_t* before_page, uint64_t offset,
                                             bool with_loaned, vm_page_t** after_page,
                                             LazyPageRequest* page_request) {
  // While the caller must have a ref on VmCowPagesContainer, the caller doesn't need to have a ref
  // on VmCowPages, for ReplacePage() in particular.
  DEBUG_ASSERT(ref_count_debug() >= 1);
  return cow().ReplacePage(before_page, offset, with_loaned, after_page, page_request);
}

template <class... Args>
void VmCowPagesContainer::EmplaceCow(Args&&... args) {
  DEBUG_ASSERT(!is_cow_present_);
  new (reinterpret_cast<VmCowPages*>(&cow_space_)) VmCowPages(ktl::forward<Args>(args)...);
  is_cow_present_ = true;
}

VmCowPages& VmCowPagesContainer::cow() {
  DEBUG_ASSERT(is_cow_present_);
  return *reinterpret_cast<VmCowPages*>(&cow_space_);
}

void VmCowPages::InitializePageCache(uint32_t level) {
  ASSERT(level < LK_INIT_LEVEL_THREADING);

  const size_t reserve_pages = 64;
  zx::result<page_cache::PageCache> result = page_cache::PageCache::Create(reserve_pages);

  ASSERT(result.is_ok());
  page_cache_ = ktl::move(result.value());
}

// Initialize the cache after the percpu data structures are initialized.
LK_INIT_HOOK(vm_cow_pages_cache_init, VmCowPages::InitializePageCache, LK_INIT_LEVEL_KERNEL + 1)
