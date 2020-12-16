// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_cow_pages.h"

#include <lib/counters.h>
#include <trace.h>

#include <fbl/auto_call.h>
#include <kernel/range_check.h>
#include <ktl/move.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/vm_object_paged.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

namespace {

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
  DEBUG_ASSERT(p->state() == VM_PAGE_STATE_ALLOC);
  p->set_state(VM_PAGE_STATE_OBJECT);
  p->object.pin_count = 0;
  p->object.cow_left_split = 0;
  p->object.cow_right_split = 0;
}

// Allocates a new page and populates it with the data at |parent_paddr|.
bool AllocateCopyPage(uint32_t pmm_alloc_flags, paddr_t parent_paddr, list_node_t* alloc_list,
                      vm_page_t** clone) {
  paddr_t pa_clone;
  vm_page_t* p_clone = nullptr;
  if (alloc_list) {
    p_clone = list_remove_head_type(alloc_list, vm_page, queue_node);
    if (p_clone) {
      pa_clone = p_clone->paddr();
    }
  }
  if (!p_clone) {
    zx_status_t status = pmm_alloc_page(pmm_alloc_flags, &p_clone, &pa_clone);
    if (!p_clone) {
      DEBUG_ASSERT(status == ZX_ERR_NO_MEMORY);
      return false;
    }
    DEBUG_ASSERT(status == ZX_OK);
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

  return true;
}

bool SlotHasPinnedPage(VmPageOrMarker* slot) {
  return slot && slot->IsPage() && slot->Page()->object.pin_count > 0;
}

inline uint64_t CheckedAdd(uint64_t a, uint64_t b) {
  uint64_t result;
  bool overflow = add_overflow(a, b, &result);
  DEBUG_ASSERT(!overflow);
  return result;
}

}  // namespace

// Helper class for collecting pages to performed batched Removes from the page queue to not incur
// its spinlock overhead for every single page. Pages that it removes from the page queue get placed
// into a provided list. Note that pages are not moved into the list until *after* Flush has been
// called and Flush must be called prior to object destruction.
class BatchPQRemove {
 public:
  BatchPQRemove(list_node_t* freed_list) : freed_list_(freed_list) {}
  ~BatchPQRemove() { DEBUG_ASSERT(count_ == 0); }
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BatchPQRemove);

  // Add a page to the batch set. Automatically calls |Flush| if the limit is reached.
  void Push(vm_page_t* page) {
    DEBUG_ASSERT(page);
    pages_[count_] = page;
    count_++;
    if (count_ == kMaxPages) {
      Flush();
    }
  }

  // Performs |Remove| on any pending pages. This allows you to know that all pages are in the
  // original list so that you can do operations on the list.
  void Flush() {
    if (count_ > 0) {
      pmm_page_queues()->RemoveArrayIntoList(pages_.data(), count_, freed_list_);
      count_ = 0;
    }
  }

  // Produces a callback suitable for passing to VmPageList::RemovePages that will |Push| any pages
  auto RemovePagesCallback() {
    return [this](VmPageOrMarker* p, uint64_t off) {
      if (p->IsPage()) {
        vm_page_t* page = p->ReleasePage();
        Push(page);
      }
      *p = VmPageOrMarker::Empty();
      return ZX_ERR_NEXT;
    };
  }

 private:
  // The value of 64 was chosen as there is minimal performance gains originally measured by using
  // higher values. There is an incentive on this being as small as possible due to this typically
  // being created on the stack, and our stack space is limited.
  static constexpr size_t kMaxPages = 64;

  size_t count_ = 0;
  ktl::array<vm_page_t*, kMaxPages> pages_;
  list_node_t* freed_list_ = nullptr;
};

VmCowPages::VmCowPages(fbl::RefPtr<VmHierarchyState> hierarchy_state_ptr, uint32_t options,
                       uint32_t pmm_alloc_flags, uint64_t size, fbl::RefPtr<PageSource> page_source)
    : VmHierarchyBase(ktl::move(hierarchy_state_ptr)),
      options_(options),
      size_(size),
      pmm_alloc_flags_(pmm_alloc_flags),
      page_source_(ktl::move(page_source)) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));
}

VmCowPages::~VmCowPages() {
  canary_.Assert();

  // To prevent races with a hidden parent creation or merging, it is necessary to hold the lock
  // over the is_hidden and parent_ check and into the subsequent removal call.
  // It is safe to grab the lock here because we are careful to never cause the last reference to
  // a VmCowPages to be dropped in this code whilst holding the lock. The single place we drop a
  // a VmCowPages reference that could trigger a deletion is in this destructor when parent_ is
  // dropped, but that is always done without holding the lock.
  Guard<Mutex> guard{&lock_};
  // If we're not a hidden vmo, then we need to remove ourself from our parent. This needs
  // to be done before emptying the page list so that a hidden parent can't merge into this
  // vmo and repopulate the page list.
  if (!is_hidden_locked()) {
    if (parent_) {
      parent_->RemoveChildLocked(this);
      guard.Release();
      // Avoid recursing destructors when we delete our parent by using the deferred deletion
      // method. See common in parent else branch for why we can avoid this on a hidden parent.
      if (!parent_->is_hidden_locked()) {
        hierarchy_state_ptr_->DoDeferredDelete(ktl::move(parent_));
      }
    }
  } else {
    // Most of the hidden vmo's state should have already been cleaned up when it merged
    // itself into its child in ::RemoveChildLocked.
    DEBUG_ASSERT(children_list_len_ == 0);
    DEBUG_ASSERT(page_list_.HasNoPages());
    // Even though we are hidden we might have a parent. Unlike in the other branch of this if we
    // do not need to perform any deferred deletion. The reason for this is that the deferred
    // deletion mechanism is intended to resolve the scenario where there is a chain of 'one ref'
    // parent pointers that will chain delete. However, with hidden parents we *know* that a hidden
    // parent has two children (and hence at least one other ref to it) and so we cannot be in a
    // one ref chain. Even if N threads all tried to remove children from the hierarchy at once,
    // this would ultimately get serialized through the lock and the hierarchy would go from
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

  // Cleanup page lists and page sources.
  list_node_t list;
  list_initialize(&list);

  BatchPQRemove page_remover(&list);
  // free all of the pages attached to us
  page_list_.RemoveAllPages([&page_remover](vm_page_t* page) {
    ASSERT(page->object.pin_count == 0);
    page_remover.Push(page);
  });

  if (page_source_) {
    page_source_->Close();
  }
  page_remover.Flush();

  pmm_free(&list);
}

bool VmCowPages::DedupZeroPage(vm_page_t* page, uint64_t offset) {
  canary_.Assert();

  Guard<Mutex> guard{&lock_};

  if (paged_ref_) {
    AssertHeld(paged_ref_->lock_ref());
    if (!paged_ref_->CanDedupZeroPagesLocked()) {
      return false;
    }
  }

  // Check this page is still a part of this VMO. object.page_offset could be complete garbage,
  // but there's no harm in looking up a random slot as we'll then notice it's the wrong page.
  VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page ||
      page->object.pin_count > 0) {
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
    RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);
    vm_page_t* page = page_or_marker->ReleasePage();
    pmm_page_queues()->Remove(page);
    DEBUG_ASSERT(!list_in_list(&page->queue_node));
    pmm_free_page(page);
    *page_or_marker = VmPageOrMarker::Marker();
    eviction_event_count_++;
    IncrementHierarchyGenerationCountLocked();
    return true;
  }
  return false;
}

uint32_t VmCowPages::ScanForZeroPagesLocked(bool reclaim) {
  canary_.Assert();

  // Check if we have any slice children. Slice children may have writable mappings to our pages,
  // and so we need to also remove any mappings from them. Non-slice children could only have
  // read-only mappings, which is the state we already want, and so we don't need to touch them.
  for (auto& child : children_list_) {
    AssertHeld(child.lock_);
    if (child.is_slice_locked()) {
      // Slices are strict subsets of their parents so we don't need to bother looking at parent
      // limits etc and can just operate on the entire range.
      child.RangeChangeUpdateLocked(0, child.size_, RangeChangeOp::RemoveWrite);
    }
  }

  list_node_t freed_list;
  list_initialize(&freed_list);

  uint32_t count = 0;
  page_list_.RemovePages(
      [&count, &freed_list, reclaim, this](VmPageOrMarker* p, uint64_t off) {
        // Pinned pages cannot be decommitted so do not consider them.
        if (p->IsPage() && p->Page()->object.pin_count == 0 && IsZeroPage(p->Page())) {
          count++;
          if (reclaim) {
            // Need to remove all mappings (include read) ones to this range before we remove the
            // page.
            AssertHeld(this->lock_);
            RangeChangeUpdateLocked(off, PAGE_SIZE, RangeChangeOp::Unmap);
            vm_page_t* page = p->ReleasePage();
            pmm_page_queues()->Remove(page);
            DEBUG_ASSERT(!list_in_list(&page->queue_node));
            list_add_tail(&freed_list, &page->queue_node);
            *p = VmPageOrMarker::Marker();
          }
        }
        return ZX_ERR_NEXT;
      },
      0, VmPageList::MAX_SIZE);

  pmm_free(&freed_list);
  return count;
}

zx_status_t VmCowPages::Create(fbl::RefPtr<VmHierarchyState> root_lock, uint32_t pmm_alloc_flags,
                               uint64_t size, fbl::RefPtr<VmCowPages>* cow_pages) {
  fbl::AllocChecker ac;
  auto cow = fbl::AdoptRef<VmCowPages>(
      new (&ac) VmCowPages(ktl::move(root_lock), 0, pmm_alloc_flags, size, nullptr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  *cow_pages = ktl::move(cow);
  return ZX_OK;
}

zx_status_t VmCowPages::CreateExternal(fbl::RefPtr<PageSource> src,
                                       fbl::RefPtr<VmHierarchyState> root_lock, uint64_t size,
                                       fbl::RefPtr<VmCowPages>* cow_pages) {
  fbl::AllocChecker ac;
  auto cow = fbl::AdoptRef<VmCowPages>(
      new (&ac) VmCowPages(ktl::move(root_lock), 0, PMM_ALLOC_FLAG_ANY, size, ktl::move(src)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
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
  auto slice = fbl::AdoptRef<VmCowPages>(
      new (&ac) VmCowPages(hierarchy_state_ptr_, kSlice, PMM_ALLOC_FLAG_ANY, size, nullptr));
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

  // All validation *must* be performed here prior to construction the VmCowPages, as the
  // destructor for VmCowPages may acquire the lock, which we are already holding.

  switch (type) {
    case CloneType::Snapshot: {
      if (!IsCowClonableLocked()) {
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
      if (!is_pager_backed_locked()) {
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
    // invoked if the second fails we separately perform allocations and construction.
    union VmCowPagesPlaceHolder {
      VmCowPagesPlaceHolder() {}
      ~VmCowPagesPlaceHolder() {}

      uint8_t trivially_destructible_default_variant;
      VmCowPages vm_cow_pages;
    };
    fbl::AllocChecker ac;
    ktl::unique_ptr<VmCowPagesPlaceHolder> left_child_placeholder =
        ktl::make_unique<VmCowPagesPlaceHolder>(&ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    ktl::unique_ptr<VmCowPagesPlaceHolder> right_child_placeholder =
        ktl::make_unique<VmCowPagesPlaceHolder>(&ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    // At this point cow_pages must *not* be destructed in this function, as doing so would cause a
    // deadlock. That means from this point on we *must* succeed and any future error checking needs
    // to be added prior to creation.

    fbl::RefPtr<VmCowPages> left_child =
        fbl::AdoptRef<VmCowPages>(new (&left_child_placeholder.release()->vm_cow_pages) VmCowPages(
            hierarchy_state_ptr_, 0, pmm_alloc_flags_, size_, nullptr));
    fbl::RefPtr<VmCowPages> right_child =
        fbl::AdoptRef<VmCowPages>(new (&right_child_placeholder.release()->vm_cow_pages) VmCowPages(
            hierarchy_state_ptr_, 0, pmm_alloc_flags_, size, nullptr));
    AssertHeld(left_child->lock_ref());
    AssertHeld(right_child->lock_ref());

    // The left child becomes a full clone of us, inheriting our children, paged backref etc.
    CloneParentIntoChildLocked(left_child);

    // The right child is the, potential, subset view into the parent so has a variable offset. If
    // this view would extend beyond us then we need to clip the parent_limit to our size_, which
    // will ensure any pages in that range just get initialized from zeroes.
    AddChildLocked(right_child.get(), offset, new_root_parent_offset, child_parent_limit);

    // Transition into being the hidden node.
    options_ |= kHidden;
    DEBUG_ASSERT(children_list_len_ == 2);

    *cow_child = ktl::move(right_child);

    return ZX_OK;
  } else {
    fbl::AllocChecker ac;
    auto cow_pages = fbl::AdoptRef<VmCowPages>(
        new (&ac) VmCowPages(hierarchy_state_ptr_, 0, pmm_alloc_flags_, size, nullptr));
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

  return ZX_OK;
}

void VmCowPages::RemoveChildLocked(VmCowPages* removed) {
  canary_.Assert();

  AssertHeld(removed->lock_);

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
    // then the vmo with the matching attribution user id  was just closed. In that case, we
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
}

void VmCowPages::MergeContentWithChildLocked(VmCowPages* removed, bool removed_left) {
  DEBUG_ASSERT(children_list_len_ == 1);
  VmCowPages& child = children_list_.front();
  AssertHeld(child.lock_);
  AssertHeld(removed->lock_);

  list_node freed_pages;
  list_initialize(&freed_pages);
  BatchPQRemove page_remover(&freed_pages);

  const uint64_t visibility_start_offset = child.parent_offset_ + child.parent_start_limit_;
  const uint64_t merge_start_offset = child.parent_offset_;
  const uint64_t merge_end_offset = child.parent_offset_ + child.parent_limit_;

  // Hidden parents are not supposed to have page sources, but we assert it here anyway because a
  // page source would make the way we move pages between objects incorrect, as we would break any
  // potential back links.
  DEBUG_ASSERT(!page_source_);

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

  // As we are moving pages between objects we need to make sure no backlinks are broken. We know
  // there's no page_source_ and hence no pages will be in the pager_backed queue, but we could
  // have pages in the unswappable_zero_forked queue. We do know that pages in this queue cannot
  // have been pinned, so we can just move (or re-move potentially) any page that is not pinned
  // into the unswappable queue.
  {
    PageQueues* pq = pmm_page_queues();
    Guard<SpinLock, IrqSave> guard{pq->get_lock()};
    page_list_.ForEveryPage([pq](auto* p, uint64_t off) {
      if (p->IsPage()) {
        vm_page_t* page = p->Page();
        if (page->object.pin_count == 0) {
          AssertHeld<Lock<SpinLock>, IrqSave>(*pq->get_lock());
          pq->MoveToUnswappableLocked(page);
        }
      }
      return ZX_ERR_NEXT;
    });
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
          if (page->IsMarker()) {
            return ZX_ERR_NEXT;
          }
          VmPageOrMarker* page_or_mark = page_list_.Lookup(offset + removed_offset);
          if (page_or_mark && page_or_mark->IsPage()) {
            vm_page* p_page = page_or_mark->Page();
            // The page is definitely forked into |removed|, but
            // shouldn't be forked twice.
            DEBUG_ASSERT(p_page->object.cow_left_split ^ p_page->object.cow_right_split);
            p_page->object.cow_left_split = 0;
            p_page->object.cow_right_split = 0;
          }
          return ZX_ERR_NEXT;
        },
        removed->parent_start_limit_, removed->parent_limit_);

    list_node covered_pages;
    list_initialize(&covered_pages);
    BatchPQRemove covered_remover(&covered_pages);

    // Now merge |child|'s pages into |this|, overwriting any pages present in |this|, and
    // then move that list to |child|.

    child.page_list_.MergeOnto(page_list_,
                               [&covered_remover](vm_page_t* p) { covered_remover.Push(p); });
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
    child.page_list_.MergeFrom(
        page_list_, merge_start_offset, merge_end_offset,
        [&page_remover](vm_page* page, uint64_t offset) { page_remover.Push(page); },
        [&page_remover, removed_left](VmPageOrMarker* page_or_marker, uint64_t offset) {
          DEBUG_ASSERT(page_or_marker->IsPage());
          vm_page_t* page = page_or_marker->Page();
          DEBUG_ASSERT(page->object.pin_count == 0);

          if (removed_left ? page->object.cow_right_split : page->object.cow_left_split) {
            // This happens when the pages was already migrated into child but then
            // was migrated further into child's descendants. The page can be freed.
            page = page_or_marker->ReleasePage();
            page_remover.Push(page);
          } else {
            // Since we recursively fork on write, if the child doesn't have the
            // page, then neither of its children do.
            page->object.cow_left_split = 0;
            page->object.cow_right_split = 0;
          }
        });
  }

  page_remover.Flush();
  if (!list_is_empty(&freed_pages)) {
    pmm_free(&freed_pages);
  }
}

void VmCowPages::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();

  size_t count = 0;
  page_list_.ForEveryPage([&count](const auto* p, uint64_t) {
    if (p->IsPage()) {
      count++;
    }
    return ZX_ERR_NEXT;
  });

  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("cow_pages %p size %#" PRIx64 " offset %#" PRIx64 " start limit %#" PRIx64
         " limit %#" PRIx64 " pages %zu ref %d parent %p\n",
         this, size_, parent_offset_, parent_start_limit_, parent_limit_, count, ref_count_debug(),
         parent_.get());

  if (page_source_) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    page_source_->Dump();
  }

  if (verbose) {
    auto f = [depth](const auto* p, uint64_t offset) {
      for (uint i = 0; i < depth + 1; ++i) {
        printf("  ");
      }
      if (p->IsMarker()) {
        printf("offset %#" PRIx64 " zero page marker\n", offset);
      } else {
        vm_page_t* page = p->Page();
        printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "(%c%c)\n", offset, page,
               page->paddr(), page->object.cow_left_split ? 'L' : '.',
               page->object.cow_right_split ? 'R' : '.');
      }
      return ZX_ERR_NEXT;
    };
    page_list_.ForEveryPage(f);
  }
}

size_t VmCowPages::AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const {
  canary_.Assert();

  if (is_hidden_locked()) {
    return 0;
  }

  size_t page_count = 0;
  // TODO: Decide who pages should actually be attribtued to.
  page_list_.ForEveryPageAndGapInRange(
      [&page_count](const auto* p, uint64_t off) {
        if (p->IsPage()) {
          page_count++;
        }
        return ZX_ERR_NEXT;
      },
      [this, &page_count](uint64_t gap_start, uint64_t gap_end) {
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
          uint64_t local_count = 0;
          uint64_t attributed =
              CountAttributedAncestorPagesLocked(off, gap_end - off, &local_count);
          // |CountAttributedAncestorPagesLocked| guarantees that it will make progress.
          DEBUG_ASSERT(attributed > 0);
          off += attributed;
          page_count += local_count;
        }

        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  return page_count;
}

uint64_t VmCowPages::CountAttributedAncestorPagesLocked(uint64_t offset, uint64_t size,
                                                        uint64_t* count) const TA_REQ(lock_) {
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
  uint64_t attributed_ours = 0;
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
          vm_page* page = p->Page();
          if (
              // Page is explicitly owned by us
              (parent->page_attribution_user_id_ == cur->page_attribution_user_id_) ||
              // If page has already been split and we can see it, then we know
              // the sibling subtree can't see the page and thus it should be
              // attributed to this vmo.
              (page->object.cow_left_split || page->object.cow_right_split) ||
              // If the sibling cannot access this page then its ours, otherwise we know there's
              // a vmo in the sibling subtree which is 'closer' to this offset, and to which we will
              // attribute the page to.
              !(sib.parent_offset_ + sib.parent_start_limit_ <= off &&
                off < sib.parent_offset_ + sib.parent_limit_)) {
            attributed_ours++;
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

zx_status_t VmCowPages::AddPageLocked(VmPageOrMarker* p, uint64_t offset, bool do_range_update) {
  canary_.Assert();

  if (p->IsPage()) {
    LTRACEF("vmo %p, offset %#" PRIx64 ", page %p (%#" PRIxPTR ")\n", this, offset, p->Page(),
            p->Page()->paddr());
  } else {
    DEBUG_ASSERT(p->IsMarker());
    LTRACEF("vmo %p, offset %#" PRIx64 ", marker\n", this, offset);
  }

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  VmPageOrMarker* page = page_list_.LookupOrAllocate(offset);
  if (!page) {
    return ZX_ERR_NO_MEMORY;
  }
  // Only fail on pages, we overwrite markers and empty slots.
  if (page->IsPage()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  // If this is actually a real page, we need to place it into the appropriate queue.
  if (p->IsPage()) {
    vm_page_t* page = p->Page();
    DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
    DEBUG_ASSERT(page->object.pin_count == 0);
    SetNotWired(page, offset);
  }
  *page = ktl::move(*p);

  if (do_range_update) {
    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);
  }

  return ZX_OK;
}

zx_status_t VmCowPages::AddNewPageLocked(uint64_t offset, vm_page_t* page, bool zero,
                                         bool do_range_update) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));

  InitializeVmPage(page);
  if (zero) {
    ZeroPage(page);
  }

  VmPageOrMarker p = VmPageOrMarker::Page(page);
  zx_status_t status = AddPageLocked(&p, offset, false);

  if (status != ZX_OK) {
    // Release the page from 'p', as we are returning failure 'page' is still owned by the caller.
    p.ReleasePage();
  }
  return status;
}

zx_status_t VmCowPages::AddNewPagesLocked(uint64_t start_offset, list_node_t* pages, bool zero,
                                          bool do_range_update) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset));

  uint64_t offset = start_offset;
  while (vm_page_t* p = list_remove_head_type(pages, vm_page_t, queue_node)) {
    // Defer the range change update by passing false as we will do it in bulk at the end if needed.
    zx_status_t status = AddNewPageLocked(offset, p, zero, false);
    if (status != ZX_OK) {
      // Put the page back on the list so that someone owns it and it'll get free'd.
      list_add_head(pages, &p->queue_node);
      // Decommit any pages we already placed.
      if (offset > start_offset) {
        DecommitRangeLocked(start_offset, offset - start_offset);
      }

      // Free all the pages back as we had ownership of them.
      pmm_free(pages);
      return status;
    }
    offset += PAGE_SIZE;
  }

  if (do_range_update) {
    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(start_offset, offset - start_offset, RangeChangeOp::Unmap);
  }
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

vm_page_t* VmCowPages::CloneCowPageLocked(uint64_t offset, list_node_t* alloc_list,
                                          VmCowPages* page_owner, vm_page_t* page,
                                          uint64_t owner_offset) {
  DEBUG_ASSERT(page != vm_get_zero_page());
  DEBUG_ASSERT(parent_);

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

  bool alloc_failure = false;

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

      target_page->object.cow_left_split = 0;
      target_page->object.cow_right_split = 0;
      VmPageOrMarker removed = target_page_owner->page_list_.RemovePage(target_page_offset);
      vm_page* removed_page = removed.ReleasePage();
      pmm_page_queues()->Remove(removed_page);
      DEBUG_ASSERT(removed_page == target_page);
    } else {
      // Otherwise we need to fork the page.
      vm_page_t* cover_page;
      alloc_failure = !AllocateCopyPage(pmm_alloc_flags_, page->paddr(), alloc_list, &cover_page);
      if (unlikely(alloc_failure)) {
        // TODO: plumb through PageRequest once anonymous page source is implemented.
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
    zx_status_t status = cur->AddPageLocked(&add_page, cur_offset, false);
    DEBUG_ASSERT(status == ZX_OK);

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
  DEBUG_ASSERT(alloc_failure || cur_offset == offset);

  if (unlikely(alloc_failure)) {
    return nullptr;
  } else {
    return target_page;
  }
}

zx_status_t VmCowPages::CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* freed_list,
                                                 VmCowPages* page_owner, vm_page_t* page,
                                                 uint64_t owner_offset) {
  DEBUG_ASSERT(parent_);

  // Ensure we have a slot as we'll need it later.
  VmPageOrMarker* slot = page_list_.LookupOrAllocate(offset);

  if (!slot) {
    return ZX_ERR_NO_MEMORY;
  }

  // We cannot be forking a page to here if there's already something.
  DEBUG_ASSERT(slot->IsEmpty());

  // Need to make sure the page is duplicated as far as our parent. Then we can pretend
  // that we have forked it into us by setting the marker.
  AssertHeld(parent_->lock_);
  if (page_owner != parent_.get()) {
    // Do not pass our freed_list here as this wants an alloc_list to allocate from.
    page = parent_->CloneCowPageLocked(offset + parent_offset_, nullptr, page_owner, page,
                                       owner_offset);
    if (page == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  bool left = this == &(parent_->left_child_locked());
  // Page is in our parent. Check if its uni accessible, if so we can free it.
  if (parent_->IsUniAccessibleLocked(page, offset + parent_offset_)) {
    // Make sure we didn't already merge the page in this direction.
    DEBUG_ASSERT(!(left && page->object.cow_left_split));
    DEBUG_ASSERT(!(!left && page->object.cow_right_split));
    vm_page* removed = parent_->page_list_.RemovePage(offset + parent_offset_).ReleasePage();
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
  *slot = VmPageOrMarker::Marker();
  return ZX_OK;
}

VmPageOrMarker* VmCowPages::FindInitialPageContentLocked(uint64_t offset, VmCowPages** owner_out,
                                                         uint64_t* owner_offset_out) {
  // Search up the clone chain for any committed pages. cur_offset is the offset
  // into cur we care about. The loop terminates either when that offset contains
  // a committed page or when that offset can't reach into the parent.
  VmPageOrMarker* page = nullptr;
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

    cur = parent;
    cur_offset = parent_offset;
    VmPageOrMarker* p = cur->page_list_.Lookup(parent_offset);
    if (p && !p->IsEmpty()) {
      page = p;
      break;
    }
  }

  *owner_out = cur;
  *owner_offset_out = cur_offset;

  return page;
}

void VmCowPages::UpdateOnAccessLocked(vm_page_t* page, uint64_t offset) {
  // The only kinds of pages where there is anything to update on an access is pager backed pages.
  // To that end we first want to determine, with certainty, that the provided page is in fact in
  // the pager backed queue.

  if (page == vm_get_zero_page()) {
    return;
  }
  // Check if we have a page_source_. If we don't have one then none of our pages can be pager
  // backed, so we can abort.
  if (!page_source_) {
    return;
  }
  // We know there is a page source and so most of the pages will be in the pager backed queue, with
  // the exception of any pages that are pinned, those will be in the wired queue and so we need to
  // skip them.
  if (page->object.pin_count != 0) {
    return;
  }

  // These asserts are for sanity, the above checks should have caused us to abort if these aren't
  // true.
  DEBUG_ASSERT(page->object.get_object() == reinterpret_cast<void*>(this));
  DEBUG_ASSERT(page->object.get_page_offset() == offset);
  // Although the page is already in the pager backed queue, this move causes it be moved to the
  // front of the first queue, representing it was recently accessed.
  pmm_page_queues()->MoveToPagerBacked(page, this, offset);
}

// Looks up the page at the requested offset, faulting it in if requested and necessary.  If
// this VMO has a parent and the requested page isn't found, the parent will be searched.
//
// |alloc_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |alloc_list| is a non-empty list, faulting in was requested,
// and offset is in range.
zx_status_t VmCowPages::GetPageLocked(uint64_t offset, uint pf_flags, list_node* alloc_list,
                                      PageRequest* page_request, vm_page_t** const page_out,
                                      paddr_t* const pa_out) {
  canary_.Assert();
  DEBUG_ASSERT(!is_hidden_locked());

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  offset = ROUNDDOWN(offset, PAGE_SIZE);

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->GetPageLocked(offset + parent_offset, pf_flags, alloc_list, page_request,
                                 page_out, pa_out);
  }

  VmPageOrMarker* page_or_mark = page_list_.Lookup(offset);
  vm_page* p = nullptr;
  VmCowPages* page_owner;
  uint64_t owner_offset;
  if (page_or_mark && page_or_mark->IsPage()) {
    // This is the common case where we have the page and don't need to do anything more, so
    // return it straight away.
    vm_page_t* p = page_or_mark->Page();
    UpdateOnAccessLocked(p, offset);
    if (page_out) {
      *page_out = p;
    }
    if (pa_out) {
      *pa_out = p->paddr();
    }
    return ZX_OK;
  }

  // Get content from parent if available, otherwise accept we are the owner of the yet to exist
  // page.
  if ((!page_or_mark || page_or_mark->IsEmpty()) && parent_) {
    page_or_mark = FindInitialPageContentLocked(offset, &page_owner, &owner_offset);
  } else {
    page_owner = this;
    owner_offset = offset;
  }

  // At this point we might not have an actual page, but we should at least have a notional owner.
  DEBUG_ASSERT(page_owner);

  __UNUSED char pf_string[5];
  LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
          vmm_pf_flags_to_string(pf_flags, pf_string));

  // We need to turn this potential page or marker into a real vm_page_t. This means failing cases
  // that we cannot handle, determining whether we can substitute the zero_page and potentially
  // consulting a page_source.
  if (page_or_mark && page_or_mark->IsPage()) {
    p = page_or_mark->Page();
  } else {
    // If we don't have a real page and we're not sw or hw faulting in the page, return not found.
    if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    // We need to get a real page as our initial content. At this point we are either starting from
    // the zero page, or something supplied from a page source. The page source only fills in if we
    // have a true absence of content.
    if ((page_or_mark && page_or_mark->IsMarker()) || !page_owner->page_source_) {
      // Either no relevant page source or this is a known marker, in which case the content is
      // the zero page.
      p = vm_get_zero_page();
    } else {
      AssertHeld(page_owner->lock_);
      uint64_t user_id = 0;
      if (page_owner->paged_ref_) {
        AssertHeld(page_owner->paged_ref_->lock_ref());
        user_id = page_owner->paged_ref_->user_id_locked();
      }
      VmoDebugInfo vmo_debug_info = {.vmo_ptr = reinterpret_cast<uintptr_t>(page_owner->paged_ref_),
                                     .vmo_id = user_id};
      zx_status_t status = page_owner->page_source_->GetPage(owner_offset, page_request,
                                                             vmo_debug_info, &p, nullptr);
      // Pager page sources will never synchronously return a page.
      DEBUG_ASSERT(status != ZX_OK);

      return status;
    }
  }

  // If we made it this far we must have some valid vm_page in |p|. Although this may be the zero
  // page, the rest of this function is tolerant towards correctly forking it.
  DEBUG_ASSERT(p);
  // It's possible that we are going to fork the page, and the user isn't actually going to directly
  // use `p`, but creating the fork still uses `p` so we want to consider it accessed.
  AssertHeld(page_owner->lock_);
  page_owner->UpdateOnAccessLocked(p, owner_offset);

  if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
    // If we're read-only faulting, return the page so they can map or read from it directly.
    if (page_out) {
      *page_out = p;
    }
    if (pa_out) {
      *pa_out = p->paddr();
    }
    LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, p->paddr());
    return ZX_OK;
  }

  vm_page_t* res_page;
  if (!page_owner->is_hidden_locked() || p == vm_get_zero_page()) {
    // If the vmo isn't hidden, we can't move the page. If the page is the zero
    // page, there's no need to try to move the page. In either case, we need to
    // allocate a writable page for this vmo.
    if (!AllocateCopyPage(pmm_alloc_flags_, p->paddr(), alloc_list, &res_page)) {
      return ZX_ERR_NO_MEMORY;
    }
    VmPageOrMarker insert = VmPageOrMarker::Page(res_page);
    zx_status_t status = AddPageLocked(&insert, offset);
    if (status != ZX_OK) {
      // AddPageLocked failing for any other reason is a programming error.
      DEBUG_ASSERT_MSG(status == ZX_ERR_NO_MEMORY, "status=%d\n", status);
      pmm_free_page(insert.ReleasePage());
      return status;
    }
    // Interpret a software fault as an explicit desire to have potential zero pages and don't
    // consider them for cleaning, this is an optimization.
    // We explicitly must *not* place pages from a page_source_ into the zero scanning queue.
    if (p == vm_get_zero_page() && !page_source_ && !(pf_flags & VMM_PF_FLAG_SW_FAULT)) {
      pmm_page_queues()->MoveToUnswappableZeroFork(res_page, this, offset);
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
    res_page = CloneCowPageLocked(offset, alloc_list, page_owner, p, owner_offset);
    if (res_page == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  LTRACEF("faulted in page %p, pa %#" PRIxPTR "\n", res_page, res_page->paddr());

  if (page_out) {
    *page_out = res_page;
  }
  if (pa_out) {
    *pa_out = res_page->paddr();
  }

  // If we made it here, we committed a new page in this VMO.
  IncrementHierarchyGenerationCountLocked();

  return ZX_OK;
}

zx_status_t VmCowPages::CommitRangeLocked(uint64_t offset, uint64_t len, uint64_t* committed_len,
                                          PageRequest* page_request) {
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
    if (status != ZX_OK) {
      return status;
    }
  }

  auto list_cleanup = fbl::MakeAutoCall([&page_list]() {
    if (!list_is_empty(&page_list)) {
      pmm_free(&page_list);
    }
  });

  const uint64_t start_offset = offset;
  const uint64_t end = offset + len;
  bool have_page_request = false;
  while (offset < end) {
    // Don't commit if we already have this page
    VmPageOrMarker* p = page_list_.Lookup(offset);
    if (!p || !p->IsPage()) {
      // Check if our parent has the page
      const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
      zx_status_t res = GetPageLocked(offset, flags, &page_list, page_request, nullptr, nullptr);
      if (unlikely(res == ZX_ERR_SHOULD_WAIT)) {
        // We can end up here in two cases:
        // 1. We were in batch mode but had to terminate the batch early.
        // 2. We hit the first missing page and we were not in batch mode.
        //
        // If we do have a page request, that means the batch was terminated early by pre-populated
        // pages (case 1). Return immediately.
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
      } else if (unlikely(res == ZX_ERR_NEXT)) {
        // In batch mode, will need to finalize the request later.
        if (!have_page_request) {
          // Stash how much we have committed right now, as we are going to have to reprocess this
          // range so we do not want to claim it was committed.
          *committed_len = offset - start_offset;
          have_page_request = true;
        }
      } else if (unlikely(res != ZX_OK)) {
        return res;
      }
    }

    offset += PAGE_SIZE;
  }

  if (have_page_request) {
    // commited_len was set when have_page_request was set so can just return.
    return root_source->FinalizeRequest(page_request);
  }

  // Processed the full range successfully
  *committed_len = len;
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

  // Tracks our expected page offset when iterating to ensure all pages are present.
  uint64_t next_offset = offset;

  // Should any errors occur we need to unpin everything.
  auto pin_cleanup = fbl::MakeAutoCall([this, offset, &next_offset]() {
    if (next_offset > offset) {
      AssertHeld(*lock());
      UnpinLocked(offset, next_offset - offset);
    }
  });

  zx_status_t status = page_list_.ForEveryPageInRange(
      [&next_offset](const VmPageOrMarker* p, uint64_t page_offset) {
        if (page_offset != next_offset || !p->IsPage()) {
          return ZX_ERR_BAD_STATE;
        }
        vm_page_t* page = p->Page();
        DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
        if (page->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
          return ZX_ERR_UNAVAILABLE;
        }

        page->object.pin_count++;
        if (page->object.pin_count == 1) {
          pmm_page_queues()->MoveToWired(page);
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

  if (parent_ || GetRootPageSourceLocked()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Demand offset and length be correctly aligned to not give surprising user semantics.
  if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return UnmapAndRemovePagesLocked(offset, new_len);
}

zx_status_t VmCowPages::UnmapAndRemovePagesLocked(uint64_t offset, uint64_t len) {
  // TODO(teisenbe): Allow decommitting of pages pinned by
  // CommitRangeContiguous
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
  // The only other callsite, DetachSourceLocked(), can only be called on a root pager-backed VMO.
  DEBUG_ASSERT(!parent_);

  // unmap all of the pages in this range on all the mapping regions
  RangeChangeUpdateLocked(offset, len, RangeChangeOp::Unmap);

  list_node_t freed_list;
  list_initialize(&freed_list);

  BatchPQRemove page_remover(&freed_list);

  page_list_.RemovePages(page_remover.RemovePagesCallback(), offset, offset + len);
  page_remover.Flush();
  pmm_free(&freed_list);

  return ZX_OK;
}

bool VmCowPages::PageWouldReadZeroLocked(uint64_t page_offset) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_offset));
  DEBUG_ASSERT(page_offset < size_);
  VmPageOrMarker* slot = page_list_.Lookup(page_offset);
  if (slot && slot->IsMarker()) {
    // This is already considered zero as there's a marker.
    return true;
  }
  // If we don't have a committed page we need to check our parent.
  if (!slot || !slot->IsPage()) {
    VmCowPages* page_owner;
    uint64_t owner_offset;
    if (!FindInitialPageContentLocked(page_offset, &page_owner, &owner_offset)) {
      // Parent doesn't have a page either, so would also read as zero, assuming no page source.
      return GetRootPageSourceLocked() == nullptr;
    }
  }
  // Content either locally or in our parent, assume it is non-zero and return false.
  return false;
}

zx_status_t VmCowPages::ZeroPagesLocked(uint64_t page_start_base, uint64_t page_end_base) {
  canary_.Assert();

  DEBUG_ASSERT(page_start_base <= page_end_base);
  DEBUG_ASSERT(page_end_base <= size_);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_start_base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_end_base));

  // Forward any operations on slices up to the original non slice parent.
  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->ZeroPagesLocked(page_start_base + parent_offset, page_end_base + parent_offset);
  }

  // First try and do the more efficient decommit. We prefer/ decommit as it performs work in the
  // order of the number of committed pages, instead of work in the order of size of the range. An
  // error from DecommitRangeLocked indicates that the VMO is not of a form that decommit can safely
  // be performed without exposing data that we shouldn't between children and parents, but no
  // actual state will have been changed. Should decommit succeed we are done, otherwise we will
  // have to handle each offset individually.
  zx_status_t status = DecommitRangeLocked(page_start_base, page_end_base - page_start_base);
  if (status == ZX_OK) {
    return ZX_OK;
  }

  // Unmap any page that is touched by this range in any of our, or our childrens, mapping regions.
  // We do this on the assumption we are going to be able to free pages either completely or by
  // turning them into markers and it's more efficient to unmap once in bulk here.
  RangeChangeUpdateLocked(page_start_base, page_end_base - page_start_base, RangeChangeOp::Unmap);

  list_node_t freed_list;
  list_initialize(&freed_list);

  auto auto_free = fbl::MakeAutoCall([&freed_list]() {
    if (!list_is_empty(&freed_list)) {
      pmm_free(&freed_list);
    }
  });

  // Give us easier names for our range.
  uint64_t start = page_start_base;
  uint64_t end = page_end_base;

  // If we're zeroing at the end of our parent range we can update to reflect this similar to a
  // resize. This does not work if we are a slice, but we checked for that earlier. Whilst this does
  // not actually zero the range in question, it makes future zeroing of the range far more
  // efficient, which is why we do it first.
  // parent_limit_ is a page aligned offset and so we can only reduce it to a rounded up value of
  // start.
  uint64_t rounded_start = ROUNDUP_PAGE_SIZE(start);
  if (rounded_start < parent_limit_ && end >= parent_limit_) {
    bool hidden_parent = false;
    if (parent_) {
      AssertHeld(parent_->lock_ref());
      hidden_parent = parent_->is_hidden_locked();
    }
    if (hidden_parent) {
      // Release any COW pages that are no longer necessary. This will also
      // update the parent limit.
      BatchPQRemove page_remover(&freed_list);
      ReleaseCowParentPagesLocked(rounded_start, parent_limit_, &page_remover);
      page_remover.Flush();
    } else {
      parent_limit_ = rounded_start;
    }
  }

  for (uint64_t offset = start; offset < end; offset += PAGE_SIZE) {
    VmPageOrMarker* slot = page_list_.Lookup(offset);

    const bool can_see_parent = parent_ && offset < parent_limit_;

    // This is a lambda as it only makes sense to talk about parent mutability when we have a parent
    // for this offset.
    auto parent_immutable = [can_see_parent, this]() TA_REQ(lock_) {
      DEBUG_ASSERT(can_see_parent);
      AssertHeld(parent_->lock_ref());
      return parent_->is_hidden_locked();
    };

    // Finding the initial page content is expensive, but we only need to call it
    // under certain circumstances scattered in the code below. The lambda
    // get_initial_page_content() will lazily fetch and cache the details. This
    // avoids us calling it when we don't need to, or calling it more than once.
    struct InitialPageContent {
      bool inited = false;
      VmCowPages* page_owner;
      uint64_t owner_offset;
      vm_page_t* page;
    } initial_content_;
    auto get_initial_page_content = [&initial_content_, can_see_parent, this, offset]()
                                        TA_REQ(lock_) -> const InitialPageContent& {
      if (!initial_content_.inited) {
        DEBUG_ASSERT(can_see_parent);
        VmPageOrMarker* page_or_marker = FindInitialPageContentLocked(
            offset, &initial_content_.page_owner, &initial_content_.owner_offset);
        // We only care about the parent having a 'true' vm_page for content. If the parent has a
        // marker then it's as if the parent has no content since that's a zero page anyway, which
        // is what we are trying to achieve.
        initial_content_.page =
            page_or_marker && page_or_marker->IsPage() ? page_or_marker->Page() : nullptr;
        initial_content_.inited = true;
      }
      return initial_content_;
    };

    auto parent_has_content = [get_initial_page_content]() TA_REQ(lock_) {
      return get_initial_page_content().page != nullptr;
    };

    // Ideally we just collect up pages and hand them over to the pmm all at the end, but if we need
    // to allocate any pages then we would like to ensure that we do not cause total memory to peak
    // higher due to squirreling these pages away.
    auto free_any_pages = [&freed_list] {
      if (!list_is_empty(&freed_list)) {
        pmm_free(&freed_list);
      }
    };

    // If there's already a marker then we can avoid any second guessing and leave the marker alone.
    if (slot && slot->IsMarker()) {
      continue;
    }

    // In the ideal case we can zero by making there be an Empty slot in our page list, so first
    // see if we can do that. This is true when there is nothing pinned and either:
    //  * This offset does not relate to our parent
    //  * This offset does relate to our parent, but our parent is immutable and is currently zero
    //    at this offset.
    if (!SlotHasPinnedPage(slot) &&
        (!can_see_parent || (parent_immutable() && !parent_has_content()))) {
      if (slot && slot->IsPage()) {
        vm_page_t* page = page_list_.RemovePage(offset).ReleasePage();
        pmm_page_queues()->Remove(page);
        DEBUG_ASSERT(!list_in_list(&page->queue_node));
        list_add_tail(&freed_list, &page->queue_node);
      }
      continue;
    }
    // The only time we would reach either and *not* have a parent is if the page is pinned
    DEBUG_ASSERT(SlotHasPinnedPage(slot) || parent_);

    // Now we know that we need to do something active to make this zero, either through a marker or
    // a page. First make sure we have a slot to modify.
    if (!slot) {
      slot = page_list_.LookupOrAllocate(offset);
      if (unlikely(!slot)) {
        return ZX_ERR_NO_MEMORY;
      }
    }

    // Ideally we will use a marker, but we can only do this if we can point to a committed page
    // to justify the allocation of the marker (i.e. we cannot allocate infinite markers with no
    // committed pages). A committed page in this case exists if the parent has any content.
    if (SlotHasPinnedPage(slot) || !parent_has_content()) {
      if (slot->IsPage()) {
        // Zero the existing page.
        ZeroPage(slot->Page());
        continue;
      }
      // Allocate a new page, it will be zeroed in the process.
      vm_page_t* p;
      free_any_pages();
      // Do not pass our freed_list here as this takes an |alloc_list| list to allocate from.
      bool result = AllocateCopyPage(pmm_alloc_flags_, vm_get_zero_page_paddr(), nullptr, &p);
      if (!result) {
        return ZX_ERR_NO_MEMORY;
      }
      SetNotWired(p, offset);
      *slot = VmPageOrMarker::Page(p);
      continue;
    }
    DEBUG_ASSERT(parent_ && parent_has_content());

    // We are able to insert a marker, but if our page content is from a hidden owner we need to
    // perform slightly more complex cow forking.
    const InitialPageContent& content = get_initial_page_content();
    AssertHeld(content.page_owner->lock_ref());
    if (slot->IsEmpty() && content.page_owner->is_hidden_locked()) {
      free_any_pages();
      zx_status_t result = CloneCowPageAsZeroLocked(offset, &freed_list, content.page_owner,
                                                    content.page, content.owner_offset);
      if (result != ZX_OK) {
        return result;
      }
      continue;
    }

    // Remove any page that could be hanging around in the slot before we make it a marker.
    if (slot->IsPage()) {
      vm_page_t* page = slot->ReleasePage();
      DEBUG_ASSERT(page->object.pin_count == 0);
      pmm_page_queues()->Remove(page);
      DEBUG_ASSERT(!list_in_list(&page->queue_node));
      list_add_tail(&freed_list, &page->queue_node);
    }
    *slot = VmPageOrMarker::Marker();
  }

  return ZX_OK;
}

void VmCowPages::MoveToNotWired(vm_page_t* page, uint64_t offset) {
  if (page_source_) {
    pmm_page_queues()->MoveToPagerBacked(page, this, offset);
  } else {
    pmm_page_queues()->MoveToUnswappable(page);
  }
}

void VmCowPages::SetNotWired(vm_page_t* page, uint64_t offset) {
  if (page_source_) {
    pmm_page_queues()->SetPagerBacked(page, this, offset);
  } else {
    pmm_page_queues()->SetUnswappable(page);
  }
}

void VmCowPages::UnpinPage(vm_page_t* page, uint64_t offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  ASSERT(page->object.pin_count > 0);
  page->object.pin_count--;
  if (page->object.pin_count == 0) {
    MoveToNotWired(page, offset);
  }
}

void VmCowPages::PromoteRangeForReclamationLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // We will have pages only if we are directly backed by a pager source.
  if (!page_source_) {
    return;
  }

  const uint64_t start_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_offset = ROUNDUP(offset + len, PAGE_SIZE);
  page_list_.ForEveryPageInRange(
      [](const auto* p, uint64_t) {
        if (p->IsPage()) {
          pmm_page_queues()->MoveToEndOfPagerBacked(p->Page());
        }
        return ZX_ERR_NEXT;
      },
      start_offset, end_offset);
}

void VmCowPages::UnpinLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // verify that the range is within the object
  ASSERT(InRange(offset, len, size_));
  // forbid zero length unpins as zero length pins return errors.
  ASSERT(len != 0);

  if (is_slice_locked()) {
    uint64_t parent_offset;
    VmCowPages* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->UnpinLocked(offset + parent_offset, len);
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [this](const auto* page, uint64_t off) {
        if (page->IsMarker()) {
          return ZX_ERR_NOT_FOUND;
        }
        AssertHeld(lock_);
        UnpinPage(page->Page(), off);
        return ZX_ERR_NEXT;
      },
      [](uint64_t gap_start, uint64_t gap_end) { return ZX_ERR_NOT_FOUND; }, start_page_offset,
      end_page_offset);
  ASSERT_MSG(status == ZX_OK, "Tried to unpin an uncommitted page");

  bool overflow = sub_overflow(
      pinned_page_count_, (end_page_offset - start_page_offset) / PAGE_SIZE, &pinned_page_count_);
  ASSERT(!overflow);

  return;
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
        vm_page* page = page_or_mark->Page();
        // If the sibling can still see this page then we need to keep it around, otherwise we can
        // free it. The sibling can see the page if this range is |sibling_visible| and if the
        // sibling hasn't already forked the page, which is recorded in the split bits.
        if (!sibling_visible || left ? page->object.cow_right_split : page->object.cow_left_split) {
          page = page_or_mark->ReleasePage();
          page_remover->Push(page);
          return ZX_ERR_NEXT;
        }
        if (skip_split_bits) {
          // If we were able to update this vmo's parent limit, that made the pages
          // uniaccessible. We clear the split bits to allow ::RemoveChildLocked to efficiently
          // merge vmos without having to worry about pages above parent_limit_.
          page->object.cow_left_split = 0;
          page->object.cow_right_split = 0;
        } else {
          // Otherwise set the appropriate split bit to make the page uniaccessible.
          if (left) {
            page->object.cow_left_split = 1;
          } else {
            page->object.cow_right_split = 1;
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

zx_status_t VmCowPages::ResizeLocked(uint64_t s) {
  canary_.Assert();

  LTRACEF("vmcp %p, size %" PRIu64 "\n", this, s);

  // make sure everything is aligned before we get started
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(s));
  DEBUG_ASSERT(!is_slice_locked());

  list_node_t freed_list;
  list_initialize(&freed_list);

  BatchPQRemove page_remover(&freed_list);

  // see if we're shrinking or expanding the vmo
  if (s < size_) {
    // shrinking
    uint64_t start = s;
    uint64_t end = size_;
    uint64_t len = end - start;

    // bail if there are any pinned pages in the range we're trimming
    if (AnyPagesPinnedLocked(start, len)) {
      return ZX_ERR_BAD_STATE;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(start, len, RangeChangeOp::Unmap);

    if (page_source_) {
      // Tell the page source that any non-resident pages that are now out-of-bounds
      // were supplied, to ensure that any reads of those pages get woken up.
      zx_status_t status = page_list_.ForEveryPageAndGapInRange(
          [](const auto* p, uint64_t off) { return ZX_ERR_NEXT; },
          [&](uint64_t gap_start, uint64_t gap_end) {
            page_source_->OnPagesSupplied(gap_start, gap_end);
            return ZX_ERR_NEXT;
          },
          start, end);
      DEBUG_ASSERT(status == ZX_OK);
    }

    bool hidden_parent = false;
    if (parent_) {
      AssertHeld(parent_->lock_ref());
      hidden_parent = parent_->is_hidden_locked();
    }
    if (hidden_parent) {
      // Release any COW pages that are no longer necessary. This will also
      // update the parent limit.
      ReleaseCowParentPagesLocked(start, end, &page_remover);
      // Validate that the parent limit was correctly updated as it should never remain larger than
      // our actual size.
      DEBUG_ASSERT(parent_limit_ <= s);
    } else {
      parent_limit_ = ktl::min(parent_limit_, s);
    }
    // If the tail of a parent disappears, the children shouldn't be able to see that region
    // again, even if the parent is later reenlarged. So update the child parent limits.
    UpdateChildParentLimitsLocked(s);

    page_list_.RemovePages(page_remover.RemovePagesCallback(), start, end);
  } else if (s > size_) {
    uint64_t temp;
    // Check that this VMOs new size would not cause it to overflow if projected onto the root.
    bool overflow = add_overflow(root_parent_offset_, s, &temp);
    if (overflow) {
      return ZX_ERR_INVALID_ARGS;
    }
    // expanding
    // figure the starting and ending page offset that is affected
    uint64_t start = size_;
    uint64_t end = s;
    uint64_t len = end - start;

    // inform all our children or mapping that there's new bits
    RangeChangeUpdateLocked(start, len, RangeChangeOp::Unmap);
  }

  // save bytewise size
  size_ = s;

  page_remover.Flush();
  pmm_free(&freed_list);

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

zx_status_t VmCowPages::LookupLocked(
    uint64_t offset, uint64_t len,
    fbl::Function<zx_status_t(uint64_t offset, paddr_t pa)> lookup_fn) {
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
    return parent_->LookupLocked(offset + parent_offset_, len, ktl::move(lookup_fn));
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
        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  *pages = page_list_.TakePages(offset, len);

  return ZX_OK;
}

zx_status_t VmCowPages::SupplyPagesLocked(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  canary_.Assert();

  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint64_t end = offset + len;

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

    // Defer individual range updates so we can do them in blocks.
    status = AddPageLocked(&src_page, offset, /*do_range_update=*/false);
    if (status == ZX_OK) {
      new_pages_len += PAGE_SIZE;
    } else {
      if (src_page.IsPage()) {
        vm_page_t* page = src_page.ReleasePage();
        DEBUG_ASSERT(!list_in_list(&page->queue_node));
        list_add_tail(&freed_list, &page->queue_node);
      }

      if (likely(status == ZX_ERR_ALREADY_EXISTS)) {
        status = ZX_OK;

        // We hit the end of a run of absent pages, so notify the pager source
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
  if (new_pages_len) {
    RangeChangeUpdateLocked(new_pages_start, new_pages_len, RangeChangeOp::Unmap);
    page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
  }

  if (!list_is_empty(&freed_list)) {
    pmm_free(&freed_list);
  }

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

  // |error_status| must have already been validated by the PagerDispatcher.
  DEBUG_ASSERT(PageSource::IsValidFailureCode(error_status));

  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  page_source_->OnPagesFailed(offset, len, error_status);
  return ZX_OK;
}

fbl::RefPtr<PageSource> VmCowPages::GetRootPageSourceLocked() const {
  auto cow_pages = this;
  AssertHeld(cow_pages->lock_);
  while (cow_pages->parent_) {
    cow_pages = cow_pages->parent_.get();
    if (!cow_pages) {
      return nullptr;
    }
  }
  return cow_pages->page_source_;
}

void VmCowPages::DetachSourceLocked() {
  DEBUG_ASSERT(page_source_);
  page_source_->Detach();

  // Remove committed pages so that all future page faults on this VMO and its clones can fail.
  UnmapAndRemovePagesLocked(0, size_locked());

  IncrementHierarchyGenerationCountLocked();
}

bool VmCowPages::IsCowClonableLocked() const {
  // Copy-on-write clones of pager vmos or their descendants aren't supported as we can't
  // efficiently make an immutable snapshot.
  if (is_pager_backed_locked()) {
    return false;
  }

  // Copy-on-write clones of slices aren't supported at the moment due to the resulting VMO chains
  // having non hidden VMOs between hidden VMOs. This case cannot be handled be CloneCowPageLocked
  // at the moment and so we forbid the construction of such cases for the moment.
  // Bug: 36841
  if (is_slice_locked()) {
    return false;
  }

  return true;
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

  RangeChangeList list;
  this->range_change_offset_ = offset;
  this->range_change_len_ = len;
  list.push_front(this);
  RangeChangeUpdateListLocked(&list, op);
}

bool VmCowPages::EvictPage(vm_page_t* page, uint64_t offset) {
  // Without a page source to bring the page back in we cannot even think about eviction.
  if (!page_source_) {
    return false;
  }

  Guard<Mutex> guard{&lock_};

  // Check this page is still a part of this VMO.
  VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page) {
    return false;
  }

  // Pinned pages could be in use by DMA so we cannot safely evict them.
  if (page->object.pin_count != 0) {
    return false;
  }

  // Remove any mappings to this page before we remove it.
  RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);

  // Use RemovePage over just writing to page_or_marker so that the page list has the opportunity
  // to release any now empty intermediate nodes.
  vm_page_t* p = page_list_.RemovePage(offset).ReleasePage();
  DEBUG_ASSERT(p == page);
  pmm_page_queues()->Remove(page);

  eviction_event_count_++;
  IncrementHierarchyGenerationCountLocked();
  // |page| is now owned by the caller.
  return true;
}

bool VmCowPages::DebugValidatePageSplitsLocked() const {
  canary_.Assert();

  if (!is_hidden_locked()) {
    // Nothing to validate on a leaf vmo.
    return true;
  }
  // Assume this is valid until we prove otherwise.
  bool valid = true;
  page_list_.ForEveryPage([this, &valid](const VmPageOrMarker* page, uint64_t offset) {
    if (!page->IsPage()) {
      return ZX_ERR_NEXT;
    }
    vm_page_t* p = page->Page();
    AssertHeld(this->lock_);
    // We found a page in the hidden VMO, if it has been forked in either direction then we
    // expect that if we search down that path we will find that the forked page and that no
    // descendant can 'see' back to this page.
    const VmCowPages* expected = nullptr;
    if (p->object.cow_left_split) {
      expected = &left_child_locked();
    } else if (p->object.cow_right_split) {
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
          printf("Failed to find fork of page %p (off %p) from %p in leaf node %p (off %p)\n", p,
                 (void*)offset, this, cur, (void*)(off - cur->parent_offset_));
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
    return ZX_ERR_NEXT;
  });
  return valid;
}
