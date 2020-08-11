// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_object_paged.h"

#include <align.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
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
bool AllocateCopyPage(uint32_t pmm_alloc_flags, paddr_t parent_paddr, list_node_t* free_list,
                      vm_page_t** clone) {
  paddr_t pa_clone;
  vm_page_t* p_clone = nullptr;
  if (free_list) {
    p_clone = list_remove_head_type(free_list, vm_page, queue_node);
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
  BatchPQRemove(list_node_t* free_list) : free_list_(free_list) {}
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
      pmm_page_queues()->RemoveArrayIntoList(pages_.data(), count_, free_list_);
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
    };
  }

 private:
  // The value of 64 was chosen as there is minimal performance gains originally measured by using
  // higher values. There is an incentive on this being as small as possible due to this typically
  // being created on the stack, and our stack space is limited.
  static constexpr size_t kMaxPages = 64;

  size_t count_ = 0;
  ktl::array<vm_page_t*, kMaxPages> pages_;
  list_node_t* free_list_ = nullptr;
};

VmObjectPaged::VmObjectPaged(uint32_t options, uint32_t pmm_alloc_flags, uint64_t size,
                             fbl::RefPtr<vm_lock_t> root_lock, fbl::RefPtr<PageSource> page_source)
    : VmObject(ktl::move(root_lock)),
      options_(options),
      size_(size),
      pmm_alloc_flags_(pmm_alloc_flags),
      page_source_(ktl::move(page_source)) {
  LTRACEF("%p\n", this);

  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));

  // Adding to the global list needs to be done at the end of the ctor, since
  // calls can be made into this object as soon as it is in that list.
  AddToGlobalList();
}

void VmObjectPaged::InitializeOriginalParentLocked(fbl::RefPtr<VmObject> parent, uint64_t offset) {
  DEBUG_ASSERT(parent_ == nullptr);
  DEBUG_ASSERT(original_parent_user_id_ == 0);

  if (parent->is_paged()) {
    AssertHeld(VmObjectPaged::AsVmObjectPaged(parent)->lock_);
    page_list_.InitializeSkew(VmObjectPaged::AsVmObjectPaged(parent)->page_list_.GetSkew(), offset);
  }

  AssertHeld(parent->lock_ref());
  original_parent_user_id_ = parent->user_id_locked();
  parent_ = ktl::move(parent);
}

VmObjectPaged::~VmObjectPaged() {
  canary_.Assert();

  LTRACEF("%p\n", this);

  RemoveFromGlobalList();

  if (!is_hidden()) {
    // If we're not a hidden vmo, then we need to remove ourself from our parent. This needs
    // to be done before emptying the page list so that a hidden parent can't merge into this
    // vmo and repopulate the page list.
    //
    // To prevent races with a hidden parent merging itself into this vmo, it is necessary
    // to hold the lock over the parent_ check and into the subsequent removal call.
    Guard<Mutex> guard{&lock_};
    if (parent_) {
      LTRACEF("removing ourself from our parent %p\n", parent_.get());
      parent_->RemoveChild(this, guard.take());
    }
  } else {
    // Most of the hidden vmo's state should have already been cleaned up when it merged
    // itself into its child in ::RemoveChild.
    DEBUG_ASSERT(children_list_len_ == 0);
    DEBUG_ASSERT(page_list_.HasNoPages());
  }

  list_node_t list;
  list_initialize(&list);

  BatchPQRemove page_remover(&list);
  // free all of the pages attached to us
  page_list_.RemoveAllPages([this, &page_remover](vm_page_t* page) {
    page_remover.Push(page);
    if (this->is_contiguous()) {
      // Don't use unpin page since we already removed it from the page queue.
      page->object.pin_count--;
    }
    ASSERT(page->object.pin_count == 0);
  });

  if (page_source_) {
    page_source_->Close();
  }
  page_remover.Flush();

  pmm_free(&list);
}

void VmObjectPaged::HarvestAccessedBits() {
  Guard<Mutex> guard{lock()};
  // If there is no root page source, then we have nothing worth harvesting bits from.
  if (GetRootPageSourceLocked() == nullptr) {
    return;
  }

  fbl::Function<bool(vm_page_t*, uint64_t)> f = [this](vm_page_t* p, uint64_t offset) {
    AssertHeld(*lock());
    // Skip the zero page as we are never going to evict it and initial zero pages will not be
    // returned by GetPageLocked down below.
    if (p == vm_get_zero_page()) {
      return false;
    }
    // Use GetPageLocked to perform page lookup. Pass neither software fault, hardware fault or
    // write to prevent any committing or copy-on-write behavior. This will just cause the page to
    // be looked up, and its location in any pager_backed queues updated.
    __UNUSED vm_page_t* out;
    __UNUSED zx_status_t result = GetPageLocked(offset, 0, nullptr, nullptr, &out, nullptr);
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
      __UNUSED zx_status_t result = m.HarvestAccessVmoRangeLocked(0, size(), f);
      // There's no way we should be harvesting an invalid range as that would imply that this
      // mapping is invalid.
      DEBUG_ASSERT(result == ZX_OK);
    }
  }
}
bool VmObjectPaged::DedupZeroPage(vm_page_t* page, uint64_t offset) {
  Guard<Mutex> guard{&lock_};

  // Check this page is still a part of this VMO. object.page_offset could be complete garbage,
  // but there's no harm in looking up a random slot as we'll then notice it's the wrong page.
  VmPageOrMarker* page_or_marker = page_list_.Lookup(offset);
  if (!page_or_marker || !page_or_marker->IsPage() || page_or_marker->Page() != page ||
      page->object.pin_count > 0) {
    return false;
  }

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
    return true;
  }
  return false;
}

uint32_t VmObjectPaged::ScanForZeroPages(bool reclaim) {
  list_node_t free_list;
  list_initialize(&free_list);
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
    m.RemoveWriteVmoRangeLocked(0, size());
  }

  // Check if we have any slice children. Slice children may have writable mappings to our pages,
  // and so we need to also remove any mappings from them. Non-slice children could only have
  // read-only mappings, which is the state we already want, and so we don't need to touch them.
  for (auto& child : children_list_) {
    DEBUG_ASSERT(child.is_paged());
    VmObjectPaged& typed_child = static_cast<VmObjectPaged&>(child);
    if (typed_child.is_slice()) {
      // Slices are strict subsets of their parents so we don't need to bother looking at parent
      // limits etc and can just operate on the entire range.
      AssertHeld(typed_child.lock_);
      typed_child.RangeChangeUpdateLocked(0, typed_child.size(), RangeChangeOp::RemoveWrite);
    }
  }

  uint32_t count = 0;
  page_list_.ForEveryPage([&count, &free_list, reclaim, this](auto& p, uint64_t off) {
    // Pinned pages cannot be decommitted so do not consider them.
    if (p.IsPage() && p.Page()->object.pin_count == 0 && IsZeroPage(p.Page())) {
      count++;
      if (reclaim) {
        // Need to remove all mappings (include read) ones to this range before we remove the
        // page.
        AssertHeld(this->lock_);
        RangeChangeUpdateLocked(off, PAGE_SIZE, RangeChangeOp::Unmap);
        vm_page_t* page = p.ReleasePage();
        pmm_page_queues()->Remove(page);
        DEBUG_ASSERT(!list_in_list(&page->queue_node));
        list_add_tail(&free_list, &page->queue_node);
        p = VmPageOrMarker::Marker();
      }
    }
    return ZX_ERR_NEXT;
  });
  // Release the guard so we can free any pages.
  guard.Release();
  pmm_free(&free_list);
  return count;
}

zx_status_t VmObjectPaged::CreateCommon(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                        fbl::RefPtr<VmObject>* obj) {
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto lock = fbl::AdoptRef<vm_lock_t>(new (&ac) vm_lock_t);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto vmo = fbl::AdoptRef<VmObject>(
      new (&ac) VmObjectPaged(options, pmm_alloc_flags, size, ktl::move(lock), nullptr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPaged::Create(uint32_t pmm_alloc_flags, uint32_t options, uint64_t size,
                                  fbl::RefPtr<VmObject>* obj) {
  if (options & kContiguous) {
    // Force callers to use CreateContiguous() instead.
    return ZX_ERR_INVALID_ARGS;
  }

  return CreateCommon(pmm_alloc_flags, options, size, obj);
}

zx_status_t VmObjectPaged::CreateContiguous(uint32_t pmm_alloc_flags, uint64_t size,
                                            uint8_t alignment_log2, fbl::RefPtr<VmObject>* obj) {
  DEBUG_ASSERT(alignment_log2 < sizeof(uint64_t) * 8);
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<VmObject> vmo;
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
  auto cleanup_phys_pages = fbl::MakeAutoCall([&page_list]() { pmm_free(&page_list); });

  // add them to the appropriate range of the object
  VmObjectPaged* vmop = static_cast<VmObjectPaged*>(vmo.get());
  Guard<Mutex> guard{&vmop->lock_};
  for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
    VmPageOrMarker* slot = vmop->page_list_.LookupOrAllocate(off);
    if (!slot) {
      return ZX_ERR_NO_MEMORY;
    }
    if (!slot->IsEmpty()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, queue_node);
    ASSERT(p);

    InitializeVmPage(p);

    // TODO: remove once pmm returns zeroed pages
    ZeroPage(p);

    // Mark the pages as pinned, so they can't be physically rearranged
    // underneath us.
    DEBUG_ASSERT(p->object.pin_count == 0);
    p->object.pin_count++;
    pmm_page_queues()->SetWired(p);

    *slot = VmPageOrMarker::Page(p);
  }

  cleanup_phys_pages.cancel();
  *obj = ktl::move(vmo);
  return ZX_OK;
}

zx_status_t VmObjectPaged::CreateFromWiredPages(const void* data, size_t size, bool exclusive,
                                                fbl::RefPtr<VmObject>* obj) {
  LTRACEF("data %p, size %zu\n", data, size);

  fbl::RefPtr<VmObject> vmo;
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
      InitializeVmPage(page);

      // XXX hack to work around the ref pointer to the base class
      auto vmo2 = static_cast<VmObjectPaged*>(vmo.get());
      vmo2->AddPage(page, count * PAGE_SIZE);
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
                                          uint64_t size, fbl::RefPtr<VmObject>* obj) {
  // make sure size is page aligned
  zx_status_t status = RoundSize(size, &size);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto lock = fbl::AdoptRef<vm_lock_t>(new (&ac) vm_lock_t);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto vmo = fbl::AdoptRef<VmObject>(
      new (&ac) VmObjectPaged(options, PMM_ALLOC_FLAG_ANY, size, ktl::move(lock), ktl::move(src)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *obj = ktl::move(vmo);

  return ZX_OK;
}

void VmObjectPaged::InsertHiddenParentLocked(fbl::RefPtr<VmObjectPaged>&& hidden_parent) {
  AssertHeld(hidden_parent->lock_);
  // Insert the new VmObject |hidden_parent| between between |this| and |parent_|.
  if (parent_) {
    AssertHeld(parent_->lock_ref());
    hidden_parent->InitializeOriginalParentLocked(parent_, 0);
    parent_->ReplaceChildLocked(this, hidden_parent.get());
  }
  hidden_parent->AddChildLocked(this);
  parent_ = hidden_parent;

  // We use the user_id to walk the tree looking for the right child observer. This
  // is set after adding the hidden parent into the tree since that's not really
  // a 'real' child.
  hidden_parent->user_id_ = user_id_;
  hidden_parent->page_attribution_user_id_ = user_id_;

  // The hidden parent should have the same view as we had into
  // its parent, and this vmo has a full view into the hidden vmo
  hidden_parent->parent_offset_ = parent_offset_;
  hidden_parent->parent_limit_ = parent_limit_;
  parent_offset_ = 0;
  parent_limit_ = size_;

  // This method should only ever be called on leaf vmos (i.e. non-hidden),
  // so this flag should never be set.
  DEBUG_ASSERT(!partial_cow_release_);
  DEBUG_ASSERT(parent_start_limit_ == 0);  // Should only ever be set for hidden vmos

  // Moving our page list would be bad if we had a page source and potentially have pages with
  // links back to this object.
  DEBUG_ASSERT(!page_source_);
  // Move everything into the hidden parent, for immutability
  hidden_parent->page_list_ = ktl::move(page_list_);

  // As we are moving pages between objects we need to make sure no backlinks are broken. We know
  // there's no page_source_ and hence no pages will be in the pager_backed queue, but we could
  // have pages in the unswappable_zero_forked queue. We do know that pages in this queue cannot
  // have been pinned, so we can just move (or re-move potentially) any page that is not pinned
  // into the regular unswappable queue.
  {
    PageQueues* pq = pmm_page_queues();
    Guard<SpinLock, IrqSave> guard{pq->get_lock()};
    hidden_parent->page_list_.ForEveryPage([pq](auto& p, uint64_t off) {
      if (p.IsPage()) {
        vm_page_t* page = p.Page();
        if (page->object.pin_count == 0) {
          AssertHeld<Lock<SpinLock>, IrqSave>(*pq->get_lock());
          pq->MoveToUnswappableLocked(page);
        }
      }
      return ZX_ERR_NEXT;
    });
  }
  hidden_parent->size_ = size_;
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

  // Slice must be wholly contained.
  uint64_t our_size;
  {
    // size_ is not an atomic variable and although it should not be changing, as we are not
    // allowing this operation on resizable vmo's, we should still be holding the lock to
    // correctly read size_. Unfortunately we must also drop then drop the lock in order to
    // perform the allocation.
    Guard<Mutex> guard{&lock_};
    our_size = size_;
  }
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

  // There are two reasons for declaring/allocating the clones outside of the vmo's lock. First,
  // the dtor might require taking the lock, so we need to ensure that it isn't called until
  // after the lock is released. Second, diagnostics code makes calls into vmos while holding
  // the global vmo lock. Since the VmObject ctor takes the global lock, we can't construct
  // any vmos under any vmo lock.
  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPaged>(
      new (&ac) VmObjectPaged(options, pmm_alloc_flags_, size, lock_ptr_, nullptr));
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
    vmo->parent_offset_ = offset;
    vmo->parent_limit_ = size;

    vmo->InitializeOriginalParentLocked(fbl::RefPtr(this), offset);

    // add the new vmo as a child before we do anything, since its
    // dtor expects to find it in its parent's child list
    notify_one_child = AddChildLocked(vmo.get());

    if (copy_name) {
      vmo->name_ = name_;
    }
  }

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
  // There are two reasons for declaring/allocating the clones outside of the vmo's lock. First,
  // the dtor might require taking the lock, so we need to ensure that it isn't called until
  // after the lock is released. Second, diagnostics code makes calls into vmos while holding
  // the global vmo lock. Since the VmObject ctor takes the global lock, we can't construct
  // any vmos under any vmo lock.
  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPaged>(
      new (&ac) VmObjectPaged(options, pmm_alloc_flags_, size, lock_ptr_, nullptr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<VmObjectPaged> hidden_parent;
  // Optimistically create the hidden parent early as we want to do it outside the lock, but we
  // need to hold the lock to validate invariants.
  if (type == CloneType::Snapshot) {
    // The initial size is 0. It will be initialized as part of the atomic
    // insertion into the child tree.
    hidden_parent = fbl::AdoptRef<VmObjectPaged>(
        new (&ac) VmObjectPaged(kHidden, pmm_alloc_flags_, 0, lock_ptr_, nullptr));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  bool notify_one_child;
  {
    Guard<Mutex> guard{&lock_};
    AssertHeld(vmo->lock_);
    switch (type) {
      case CloneType::Snapshot: {
        // To create an eager copy-on-write clone, the kernel creates an artifical parent vmo
        // called a 'hidden vmo'. The content of the original vmo is moved into the hidden
        // vmo, and the original vmo becomes a child of the hidden vmo. Then a second child
        // is created, which is the userspace visible clone.
        //
        // Hidden vmos are an implementation detail that are not exposed to userspace.

        if (!IsCowClonableLocked()) {
          return ZX_ERR_NOT_SUPPORTED;
        }

        // If this is non-zero, that means that there are pages which hardware can
        // touch, so the vmo can't be safely cloned.
        // TODO: consider immediately forking these pages.
        if (pinned_page_count_) {
          return ZX_ERR_BAD_STATE;
        }

        break;
      }
      case CloneType::PrivatePagerCopy:
        if (!GetRootPageSourceLocked()) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        break;
    }

    // check that we're not uncached in some way
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      return ZX_ERR_BAD_STATE;
    }

    // TODO: ZX-692 make sure that the accumulated parent offset of the entire
    // parent chain doesn't wrap 64bit space.
    vmo->parent_offset_ = offset;
    if (offset > size_) {
      vmo->parent_limit_ = 0;
    } else {
      vmo->parent_limit_ = ktl::min(size, size_ - offset);
    }

    VmObjectPaged* clone_parent;
    if (type == CloneType::Snapshot) {
      clone_parent = hidden_parent.get();

      InsertHiddenParentLocked(ktl::move(hidden_parent));

      // Invalidate everything the clone will be able to see. They're COW pages now,
      // so any existing mappings can no longer directly write to the pages.
      RangeChangeUpdateLocked(vmo->parent_offset_, vmo->parent_limit_, RangeChangeOp::RemoveWrite);
    } else {
      clone_parent = this;
    }
    AssertHeld(clone_parent->lock_);

    vmo->InitializeOriginalParentLocked(fbl::RefPtr(clone_parent), offset);

    // add the new vmo as a child before we do anything, since its
    // dtor expects to find it in its parent's child list
    notify_one_child = clone_parent->AddChildLocked(vmo.get());

    if (copy_name) {
      vmo->name_ = name_;
    }
  }

  if (notify_one_child) {
    NotifyOneChild();
  }

  *child_vmo = ktl::move(vmo);

  return ZX_OK;
}

bool VmObjectPaged::OnChildAddedLocked() {
  if (!is_hidden()) {
    return VmObject::OnChildAddedLocked();
  }

  if (user_id_ == ZX_KOID_INVALID) {
    // The original vmo is added as a child of the hidden vmo before setting
    // the user id to prevent counting as its own child.
    return false;
  }

  // After initialization, hidden vmos always have two children - the vmo on which
  // zx_vmo_create_child was invoked and the vmo which that syscall created.
  DEBUG_ASSERT(children_list_len_ == 2);

  // Reaching into the children confuses analysis
  for (auto& c : children_list_) {
    DEBUG_ASSERT(c.is_paged());
    VmObjectPaged& child = static_cast<VmObjectPaged&>(c);
    AssertHeld(child.lock_);
    if (child.user_id_ == user_id_) {
      return child.OnChildAddedLocked();
    }
  }

  // One of the children should always have a matching user_id.
  panic("no child with matching user_id: %" PRIx64 "\n", user_id_);
}

void VmObjectPaged::RemoveChild(VmObject* removed, Guard<Mutex>&& adopt) {
  DEBUG_ASSERT(adopt.wraps_lock(lock_ptr_->lock.lock()));

  // This is scoped before guard to ensure the guard is dropped first, see comment where child_ref
  // is assigned for more details.
  fbl::RefPtr<VmObject> child_ref;

  Guard<Mutex> guard{AdoptLock, ktl::move(adopt)};

  if (!is_hidden()) {
    VmObject::RemoveChild(removed, guard.take());
    return;
  }

  // Hidden vmos always have 0 or 2 children, but we can't be here with 0 children.
  DEBUG_ASSERT(children_list_len_ == 2);
  // A hidden vmo must be fully initialized to have 2 children.
  DEBUG_ASSERT(user_id_ != ZX_KOID_INVALID);
  bool removed_left = &left_child_locked() == removed;

  DropChildLocked(removed);

  VmObject* child = &children_list_.front();
  DEBUG_ASSERT(child);

  // Attempt to upgrade our raw pointer to a ref ptr. This upgrade can fail in the scenario that
  // the childs refcount has dropped to zero and is also attempting to delete itself. If this
  // happens, as we hold the vmo lock we know our child cannot complete its destructor, and so we
  // can still modify pieces of it until we drop the lock. It is now possible that after we upgrade
  // we become the sole holder of a refptr, and the refptr *must* be destroyed after we release the
  // VMO lock to prevent a deadlock.
  child_ref = fbl::MakeRefPtrUpgradeFromRaw(child, guard);

  // Our children must be paged.
  DEBUG_ASSERT(child->is_paged());
  VmObjectPaged* typed_child = static_cast<VmObjectPaged*>(child);
  AssertHeld(typed_child->lock_);

  // Merge this vmo's content into the remaining child.
  DEBUG_ASSERT(removed->is_paged());
  MergeContentWithChildLocked(static_cast<VmObjectPaged*>(removed), removed_left);

  // The child which removed itself and led to the invocation should have a reference
  // to us, in addition to child.parent_ which we are about to clear.
  DEBUG_ASSERT(ref_count_debug() >= 2);

  if (typed_child->page_attribution_user_id_ != page_attribution_user_id_) {
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
      DEBUG_ASSERT(cur->parent_->is_hidden());
      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
      AssertHeld(parent->lock_);

      if (parent->page_attribution_user_id_ == page_attribution_user_id_) {
        uint64_t new_user_id = parent->left_child_locked().page_attribution_user_id_;
        if (new_user_id == user_id_to_skip) {
          new_user_id = parent->right_child_locked().page_attribution_user_id_;
        }
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
  DropChildLocked(typed_child);
  if (parent_) {
    AssertHeld(parent_->lock_ref());
    parent_->ReplaceChildLocked(this, typed_child);
  }
  typed_child->parent_ = ktl::move(parent_);

  // To use child here  we need to ensure that it will live long enough. Up until here even if child
  // was waiting to be destroyed, we knew it would stay alive as long as we held the lock. Since we
  // give away the guard in the call to OnUserChildRemoved, we can only perform the call if we can
  // separately guarantee the child stays alive by having a refptr to it.
  // In the scenario where the refptr does not exist, that means the upgrade failed and there is no
  // user object to signal anyway.
  if (child_ref) {
    // We need to proxy the closure down to the original user-visible vmo. To find
    // that, we can walk down the clone tree following the user_id_.
    VmObjectPaged* descendant = typed_child;
    AssertHeld(descendant->lock_);
    while (descendant && descendant->user_id_ == user_id_) {
      if (!descendant->is_hidden()) {
        descendant->OnUserChildRemoved(guard.take());
        return;
      }
      if (descendant->left_child_locked().user_id_ == user_id_) {
        descendant = &descendant->left_child_locked();
      } else if (descendant->right_child_locked().user_id_ == user_id_) {
        descendant = &descendant->right_child_locked();
      } else {
        descendant = nullptr;
      }
    }
  }
}

void VmObjectPaged::MergeContentWithChildLocked(VmObjectPaged* removed, bool removed_left) {
  DEBUG_ASSERT(children_list_len_ == 1);
  DEBUG_ASSERT(children_list_.front().is_paged());
  VmObjectPaged& child = static_cast<VmObjectPaged&>(children_list_.front());
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
  page_list_.RemovePages(page_remover.RemovePagesCallback(), merge_end_offset, MAX_SIZE);

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

  if (child.is_hidden()) {
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
    page_list_.ForEveryPage([pq](auto& p, uint64_t off) {
      if (p.IsPage()) {
        vm_page_t* page = p.Page();
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
  bool fast_merge = merge_start_offset == 0 && !partial_cow_release_ && !child.is_hidden();

  if (fast_merge) {
    // Only leaf vmos can be directly removed, so this must always be true. This guarantees
    // that there are no pages that were split into |removed| that have since been migrated
    // to its children.
    DEBUG_ASSERT(!removed->is_hidden());

    // Before merging, find any pages that are present in both |removed| and |this|. Those
    // pages are visibile to |child| but haven't been written to through |child|, so
    // their split bits need to be cleared. Note that ::ReleaseCowParentPagesLocked ensures
    // that pages outside of the parent limit range won't have their split bits set.
    removed->page_list_.ForEveryPageInRange(
        [removed_offset = removed->parent_offset_, this](auto& page, uint64_t offset) {
          AssertHeld(lock_);
          if (page.IsMarker()) {
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

void VmObjectPaged::DumpLocked(uint depth, bool verbose) const {
  canary_.Assert();

  uint64_t parent_id = original_parent_user_id_;

  size_t count = 0;
  page_list_.ForEveryPage([&count](const auto& p, uint64_t) {
    if (p.IsPage()) {
      count++;
    }
    return ZX_ERR_NEXT;
  });

  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("vmo %p/k%" PRIu64 " size %#" PRIx64 " offset %#" PRIx64 " start limit %#" PRIx64
         " limit %#" PRIx64 " pages %zu ref %d parent %p/k%" PRIu64 "\n",
         this, user_id_, size_, parent_offset_, parent_start_limit_, parent_limit_, count,
         ref_count_debug(), parent_.get(), parent_id);

  char name[ZX_MAX_NAME_LEN];
  get_name(name, sizeof(name));
  if (strlen(name) > 0) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    printf("name %s\n", name);
  }

  if (page_source_) {
    for (uint i = 0; i < depth + 1; ++i) {
      printf("  ");
    }
    page_source_->Dump();
  }

  if (verbose) {
    auto f = [depth](const auto& p, uint64_t offset) {
      for (uint i = 0; i < depth + 1; ++i) {
        printf("  ");
      }
      if (p.IsMarker()) {
        printf("offset %#" PRIx64 " zero page marker\n", offset);
      } else {
        vm_page_t* page = p.Page();
        printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "(%c%c)\n", offset, page,
               page->paddr(), page->object.cow_left_split ? 'L' : '.',
               page->object.cow_right_split ? 'R' : '.');
      }
      return ZX_ERR_NEXT;
    };
    page_list_.ForEveryPage(f);
  }
}

size_t VmObjectPaged::AttributedPagesInRange(uint64_t offset, uint64_t len) const {
  canary_.Assert();
  Guard<Mutex> guard{&lock_};
  return AttributedPagesInRangeLocked(offset, len);
}

size_t VmObjectPaged::AttributedPagesInRangeLocked(uint64_t offset, uint64_t len) const {
  if (is_hidden()) {
    return 0;
  }

  uint64_t new_len;
  if (!TrimRange(offset, len, size_, &new_len)) {
    return 0;
  }
  size_t count = 0;
  // TODO: Decide who pages should actually be attribtued to.
  page_list_.ForEveryPageAndGapInRange(
      [&count](const auto& p, uint64_t off) {
        if (p.IsPage()) {
          count++;
        }
        return ZX_ERR_NEXT;
      },
      [this, &count](uint64_t gap_start, uint64_t gap_end) {
        AssertHeld(lock_);

        // If there's no parent, there's no pages to care about. If there is a non-hidden
        // parent, then that owns any pages in the gap, not us.
        if (!parent_ || !parent_->is_hidden()) {
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
          count += local_count;
        }

        return ZX_ERR_NEXT;
      },
      offset, offset + new_len);

  return count;
}

uint64_t VmObjectPaged::CountAttributedAncestorPagesLocked(uint64_t offset, uint64_t size,
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
  const VmObjectPaged* cur = this;
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
    DEBUG_ASSERT(cur->parent_->is_paged());

    const auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
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
        [&parent, &cur, &attributed_ours, &sib](const auto& p, uint64_t off) {
          AssertHeld(cur->lock_);
          AssertHeld(sib.lock_);
          AssertHeld(parent->lock_);
          if (p.IsMarker()) {
            return ZX_ERR_NEXT;
          }
          vm_page* page = p.Page();
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

zx_status_t VmObjectPaged::AddPage(vm_page_t* p, uint64_t offset) {
  if (p->object.pin_count) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  VmPageOrMarker page = VmPageOrMarker::Page(p);
  zx_status_t result = AddPageLocked(&page, offset);
  if (result != ZX_OK) {
    // Leave ownership of `p` with the caller.
    page.ReleasePage();
  }
  return result;
}

zx_status_t VmObjectPaged::AddPageLocked(VmPageOrMarker* p, uint64_t offset, bool do_range_update) {
  canary_.Assert();
  DEBUG_ASSERT(lock_.lock().IsHeld());

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

bool VmObjectPaged::IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const {
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

vm_page_t* VmObjectPaged::CloneCowPageLocked(uint64_t offset, list_node_t* free_list,
                                             VmObjectPaged* page_owner, vm_page_t* page,
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
  VmObjectPaged* cur = this;
  do {
    AssertHeld(cur->lock_);
    VmObjectPaged* next = VmObjectPaged::AsVmObjectPaged(cur->parent_);
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
    VmObjectPaged* target_page_owner = cur;
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
      alloc_failure = !AllocateCopyPage(pmm_alloc_flags_, page->paddr(), free_list, &cover_page);
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
        DEBUG_ASSERT(cur->mapping_list_len_ == 0);
        VmObjectPaged& other = cur->stack_.dir_flag == StackDir::Left ? cur->right_child_locked()
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

zx_status_t VmObjectPaged::CloneCowPageAsZeroLocked(uint64_t offset, list_node_t* free_list,
                                                    VmObjectPaged* page_owner, vm_page_t* page,
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
  VmObjectPaged* typed_parent = AsVmObjectPaged(parent_);
  AssertHeld(typed_parent->lock_);
  DEBUG_ASSERT(typed_parent);

  if (page_owner != parent_.get()) {
    AssertHeld(AsVmObjectPaged(parent_)->lock_);
    // Do not pass free_list here as this wants a free_list to allocate from, where as our free_list
    // is for placing on old objects.
    page = AsVmObjectPaged(parent_)->CloneCowPageLocked(offset + parent_offset_, nullptr,
                                                        page_owner, page, owner_offset);
    if (page == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  bool left = this == &(typed_parent->left_child_locked());
  // Page is in our parent. Check if its uni accessible, if so we can free it.
  if (typed_parent->IsUniAccessibleLocked(page, offset + parent_offset_)) {
    // Make sure we didn't already merge the page in this direction.
    DEBUG_ASSERT(!(left && page->object.cow_left_split));
    DEBUG_ASSERT(!(!left && page->object.cow_right_split));
    vm_page* removed = typed_parent->page_list_.RemovePage(offset + parent_offset_).ReleasePage();
    DEBUG_ASSERT(removed == page);
    pmm_page_queues()->Remove(removed);
    DEBUG_ASSERT(!list_in_list(&removed->queue_node));
    list_add_tail(free_list, &removed->queue_node);
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

VmPageOrMarker* VmObjectPaged::FindInitialPageContentLocked(uint64_t offset, uint pf_flags,
                                                            VmObjectPaged** owner_out,
                                                            uint64_t* owner_offset_out,
                                                            uint64_t* owner_id_out) {
  // Search up the clone chain for any committed pages. cur_offset is the offset
  // into cur we care about. The loop terminates either when that offset contains
  // a committed page or when that offset can't reach into the parent.
  VmPageOrMarker* page = nullptr;
  VmObjectPaged* cur = this;
  AssertHeld(cur->lock_);
  uint64_t cur_offset = offset;
  while (cur_offset < cur->parent_limit_) {
    // If there's no parent, then parent_limit_ is 0 and we'll never enter the loop
    DEBUG_ASSERT(cur->parent_);
    VmObjectPaged* parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
    // If parent is null it means it wasn't actually paged, which shouldn't happen.
    DEBUG_ASSERT(parent);
    AssertHeld(parent->lock_ref());

    uint64_t parent_offset;
    bool overflowed = add_overflow(cur->parent_offset_, cur_offset, &parent_offset);
    ASSERT(!overflowed);
    if (parent_offset >= parent->size()) {
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
  *owner_id_out = cur->user_id_locked();

  return page;
}

void VmObjectPaged::UpdateOnAccessLocked(vm_page_t* page, uint64_t offset) {
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
// |free_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |free_list| is a non-empty list, faulting in was requested,
// and offset is in range.
zx_status_t VmObjectPaged::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                         PageRequest* page_request, vm_page_t** const page_out,
                                         paddr_t* const pa_out) {
  canary_.Assert();
  DEBUG_ASSERT(!is_hidden());

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  offset = ROUNDDOWN(offset, PAGE_SIZE);

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->GetPageLocked(offset + parent_offset, pf_flags, free_list, page_request,
                                 page_out, pa_out);
  }

  VmPageOrMarker* page_or_mark = page_list_.Lookup(offset);
  vm_page* p = nullptr;
  VmObjectPaged* page_owner;
  uint64_t owner_offset;
  uint64_t owner_id;
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
    page_or_mark =
        FindInitialPageContentLocked(offset, pf_flags, &page_owner, &owner_offset, &owner_id);
  } else {
    page_owner = this;
    owner_offset = offset;
    owner_id = user_id_locked();
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
      VmoDebugInfo vmo_debug_info = {.vmo_ptr = reinterpret_cast<uintptr_t>(page_owner),
                                     .vmo_id = owner_id};
      zx_status_t status = page_owner->page_source_->GetPage(owner_offset, page_request,
                                                             vmo_debug_info, &p, nullptr);
      // Pager page sources will never synchronously return a page.
      DEBUG_ASSERT(status != ZX_OK);

      if (page_owner != this && status == ZX_ERR_NOT_FOUND) {
        // The default behavior of clones of detached pager VMOs fault in zero
        // pages instead of propagating the pager's fault.
        // TODO: Add an arg to zx_vmo_create_child to optionally fault here.
        p = vm_get_zero_page();
      } else {
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
  if (!page_owner->is_hidden() || p == vm_get_zero_page()) {
    // If the vmo isn't hidden, we can't move the page. If the page is the zero
    // page, there's no need to try to move the page. In either case, we need to
    // allocate a writable page for this vmo.
    if (!AllocateCopyPage(pmm_alloc_flags_, p->paddr(), free_list, &res_page)) {
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
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      arch_clean_invalidate_cache_range((vaddr_t)paddr_to_physmap(res_page->paddr()), PAGE_SIZE);
    }
  } else {
    // We need a writable page; let ::CloneCowPageLocked handle inserting one.
    res_page = CloneCowPageLocked(offset, free_list, page_owner, p, owner_offset);
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

  return ZX_OK;
}

zx_status_t VmObjectPaged::CommitRangeInternal(uint64_t offset, uint64_t len, bool pin,
                                               Guard<Mutex>&& adopt) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  DEBUG_ASSERT(adopt.wraps_lock(lock_ptr_->lock.lock()));
  Guard<Mutex> guard{AdoptLock, ktl::move(adopt)};
  // Convince the static analysis that we now do actually hold lock_.
  AssertHeld(lock_);

  // If a pin is requested the entire range must exist and be valid,
  // otherwise we can commit a partial range.
  uint64_t new_len = len;
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
    if (unlikely(!InRange(offset, len, size_))) {
      return ZX_ERR_OUT_OF_RANGE;
    }
  } else {
    if (!TrimRange(offset, len, size_, &new_len)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    // was in range, just zero length
    if (new_len == 0) {
      return ZX_OK;
    }
  }

  // Child slices of VMOs are currently not resizable, nor can they be made
  // from resizable parents.  If this ever changes, the logic surrounding what
  // to do if a VMO gets resized during a Commit or Pin operation will need to
  // be revisited.  Right now, we can just rely on the fact that the initial
  // vetting/trimming of the offset and length of the operation will never
  // change if the operation is being executed against a child slice.
  DEBUG_ASSERT(!is_resizable() || !is_slice());

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);

    // PagedParentOfSliceLocked will walk all of the way up the VMO hierarchy
    // until it hits a non-slice VMO.  This guarantees that we should only ever
    // recurse once instead of an unbound number of times.  DEBUG_ASSERT this so
    // that we don't actually end up with unbound recursion just in case the
    // property changes.
    DEBUG_ASSERT(!parent->is_slice());

    return parent->CommitRangeInternal(offset + parent_offset, new_len, pin, guard.take());
  }

  // compute a page aligned end to do our searches in to make sure we cover all the pages
  uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
  DEBUG_ASSERT(end > offset);
  offset = ROUNDDOWN(offset, PAGE_SIZE);

  fbl::RefPtr<PageSource> root_source = GetRootPageSourceLocked();

  // If this vmo has a direct page source, then the source will provide the backing memory. For
  // children that eventually depend on a page source, we skip preallocating memory to avoid
  // potentially overallocating pages if something else touches the vmo while we're blocked on the
  // request. Otherwise we optimize things by preallocating all the pages.
  list_node page_list;
  list_initialize(&page_list);
  if (root_source == nullptr) {
    // make a pass through the list to find out how many pages we need to allocate
    size_t count = (end - offset) / PAGE_SIZE;
    page_list_.ForEveryPageInRange(
        [&count](const auto& p, auto off) {
          if (p.IsPage()) {
            count--;
          }
          return ZX_ERR_NEXT;
        },
        offset, end);

    if (count == 0 && !pin) {
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

  // Should any errors occur we need to unpin everything.
  auto pin_cleanup = fbl::MakeAutoCall([this, original_offset = offset, &offset, pin]() {
    // Regardless of any resizes or other things that may have happened any pinned pages *must*
    // still be within a valid range, and so we know Unpin should succeed. The edge case is if we
    // had failed to pin *any* pages and so our original offset may be outside the current range of
    // the vmo. Additionally, as pinning a zero length range is invalid, so is unpinning, and so we
    // must avoid.
    if (pin && offset > original_offset) {
      AssertHeld(*lock());
      UnpinLocked(original_offset, offset - original_offset);
    }
  });

  bool retry = false;
  PageRequest page_request(true);
  do {
    if (retry) {
      // If there was a page request that couldn't be fulfilled, we need wait on the
      // request and retry the commit. Note that when we retry the loop, offset is
      // updated past the portion of the vmo that we successfully committed.
      zx_status_t status = ZX_OK;
      guard.CallUnlocked([&page_request, &status]() mutable { status = page_request.Wait(); });
      if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
          DumpLocked(0, false);
        }
        return status;
      }
      retry = false;

      // Re-run the range checks, since size_ could have changed while we were blocked. This
      // is not a failure, since the arguments were valid when the syscall was made. It's as
      // if the commit was successful but then the pages were thrown away. Unless we are pinning,
      // in which case pages being thrown away is explicitly an error.
      new_len = len;
      if (pin) {
        // verify that the range is within the object
        if (unlikely(!InRange(offset, len, size_))) {
          return ZX_ERR_OUT_OF_RANGE;
        }
      } else {
        if (!TrimRange(offset, len, size_, &new_len)) {
          pin_cleanup.cancel();
          return ZX_OK;
        }
        if (new_len == 0) {
          pin_cleanup.cancel();
          return ZX_OK;
        }
      }

      end = ROUNDUP_PAGE_SIZE(offset + new_len);
      DEBUG_ASSERT(end > offset);
    }

    // Remember what our offset was prior to attempting to commit.
    const uint64_t prev_offset = offset;

    // cur_offset tracks how far we've made page requests, even if they're not done.
    uint64_t cur_offset = offset;
    while (cur_offset < end) {
      // Don't commit if we already have this page
      VmPageOrMarker* p = page_list_.Lookup(cur_offset);
      vm_page_t* page = nullptr;
      if (!p || !p->IsPage()) {
        // Check if our parent has the page
        const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
        zx_status_t res =
            GetPageLocked(cur_offset, flags, &page_list, &page_request, &page, nullptr);
        if (res == ZX_ERR_NEXT || res == ZX_ERR_SHOULD_WAIT) {
          // In either case we'll need to wait on the request and retry, but if we get
          // ZX_ERR_NEXT we keep faulting until we eventually see ZX_ERR_SHOULD_WAIT.
          retry = true;
          if (res == ZX_ERR_SHOULD_WAIT) {
            break;
          }
        } else if (res != ZX_OK) {
          return res;
        }
      } else {
        page = p->Page();
      }

      if (!retry) {
        // As long as we're not in the retry state cur_offset and offset should track.
        DEBUG_ASSERT(offset == cur_offset);
        // Pin the page if needed and then formally commit by increasing our working offset.
        if (pin) {
          DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
          if (page->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
            return ZX_ERR_UNAVAILABLE;
          }

          page->object.pin_count++;
          if (page->object.pin_count == 1) {
            pmm_page_queues()->MoveToWired(page);
          }
          // Pinning every page in the largest vmo possible as many times as possible can't overflow
          static_assert(VmObjectPaged::MAX_SIZE / PAGE_SIZE <
                        UINT64_MAX / VM_PAGE_OBJECT_MAX_PIN_COUNT);
          pinned_page_count_++;
        }
        offset += PAGE_SIZE;
        len -= PAGE_SIZE;
      }
      cur_offset += PAGE_SIZE;
    }

    // Unmap all of the pages in the range we touched. This may end up unmapping non-present
    // ranges or unmapping things multiple times, but it's necessary to ensure that we unmap
    // everything that actually is present before anything else sees it.
    if (cur_offset - prev_offset) {
      RangeChangeUpdateLocked(offset, cur_offset - prev_offset, RangeChangeOp::Unmap);
    }

    if (retry && cur_offset == end) {
      zx_status_t res = root_source->FinalizeRequest(&page_request);
      if (res != ZX_ERR_SHOULD_WAIT) {
        return res;
      }
    }
  } while (retry);

  pin_cleanup.cancel();
  return ZX_OK;
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);
  list_node_t list;
  list_initialize(&list);
  zx_status_t status;
  {
    Guard<Mutex> guard{&lock_};
    status = DecommitRangeLocked(offset, len, list);
  }
  if (status == ZX_OK) {
    pmm_free(&list);
  }
  return status;
}

zx_status_t VmObjectPaged::DecommitRangeLocked(uint64_t offset, uint64_t len,
                                               list_node_t& free_list) {
  if (options_ & kContiguous) {
    return ZX_ERR_NOT_SUPPORTED;
  }

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
  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    DEBUG_ASSERT(!parent->is_slice());  // assert bounded recursion.
    return parent->DecommitRangeLocked(offset + parent_offset, new_len, free_list);
  }

  if (parent_ || GetRootPageSourceLocked()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Demand offset and length be correctly aligned to not give surprising user semantics.
  if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  LTRACEF("start offset %#" PRIx64 ", end %#" PRIx64 "\n", offset, offset + new_len);

  // TODO(teisenbe): Allow decommitting of pages pinned by
  // CommitRangeContiguous

  if (AnyPagesPinnedLocked(offset, new_len)) {
    return ZX_ERR_BAD_STATE;
  }

  // unmap all of the pages in this range on all the mapping regions
  RangeChangeUpdateLocked(offset, new_len, RangeChangeOp::Unmap);

  BatchPQRemove page_remover(&free_list);

  page_list_.RemovePages(page_remover.RemovePagesCallback(), offset, offset + new_len);
  page_remover.Flush();

  return ZX_OK;
}

zx_status_t VmObjectPaged::ZeroRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  list_node_t list;
  list_initialize(&list);
  zx_status_t status;
  {
    Guard<Mutex> guard{&lock_};
    status = ZeroRangeLocked(offset, len, &list, &guard);
  }
  if (status == ZX_OK) {
    pmm_free(&list);
  } else {
    DEBUG_ASSERT(list_is_empty(&list));
  }
  return status;
}

zx_status_t VmObjectPaged::ZeroPartialPage(uint64_t page_base_offset, uint64_t zero_start_offset,
                                           uint64_t zero_end_offset, Guard<Mutex>* guard) {
  DEBUG_ASSERT(zero_start_offset <= zero_end_offset);
  DEBUG_ASSERT(zero_end_offset <= PAGE_SIZE);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(page_base_offset));
  DEBUG_ASSERT(page_base_offset < size_);

  VmPageOrMarker* slot = page_list_.Lookup(page_base_offset);

  if (slot && slot->IsMarker()) {
    // This is already considered zero so no need to redundantly zero again.
    return ZX_OK;
  }
  // If we don't have a committed page we need to check our parent.
  if (!slot || !slot->IsPage()) {
    VmObjectPaged* page_owner;
    uint64_t owner_offset, owner_id;
    if (!FindInitialPageContentLocked(page_base_offset, VMM_PF_FLAG_WRITE, &page_owner,
                                      &owner_offset, &owner_id)) {
      // Parent doesn't have a page either, so nothing to do this is already zero.
      return ZX_OK;
    }
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

zx_status_t VmObjectPaged::ZeroRangeLocked(uint64_t offset, uint64_t len, list_node_t* free_list,
                                           Guard<Mutex>* guard) {
  // Zeroing a range behaves as if it were an efficient zx_vmo_write. As we cannot write to uncached
  // vmo, we also cannot zero an uncahced vmo.
  if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
    return ZX_ERR_BAD_STATE;
  }

  // Trim the size and validate it is in range of the vmo.
  uint64_t new_len;
  if (!TrimRange(offset, len, size_, &new_len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Forward any operations on slices up to the original non slice parent.
  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->ZeroRangeLocked(offset + parent_offset, new_len, free_list, guard);
  }

  // Construct our initial range. Already checked the range above so we know it cannot overflow.
  uint64_t start = offset;
  uint64_t end = start + new_len;

  // Unmap any page that is touched by this range in any of our, or our childrens, mapping regions.
  // This ensures that for any pages we are able to zero through decommiting we do not have free'd
  // pages still being mapped in.
  RangeChangeUpdateLocked(start, end - start, RangeChangeOp::Unmap);

  // If we're zeroing at the end of our parent range we can update to reflect this similar to a
  // resize. This does not work if we are a slice, but we checked for that earlier. Whilst this does
  // not actually zero the range in question, it makes future zeroing of the range far more
  // efficient, which is why we do it first.
  // parent_limit_ is a page aligned offset and so we can only reduce it to a rounded up value of
  // start.
  uint64_t rounded_start = ROUNDUP_PAGE_SIZE(start);
  if (rounded_start < parent_limit_ && end >= parent_limit_) {
    if (parent_ && parent_->is_hidden()) {
      // Release any COW pages that are no longer necessary. This will also
      // update the parent limit.
      BatchPQRemove page_remover(free_list);
      ReleaseCowParentPagesLocked(rounded_start, parent_limit_, &page_remover);
      page_remover.Flush();
    } else {
      parent_limit_ = rounded_start;
    }
  }

  // Helper that checks and establishes our invariants. We use this after calling functions that
  // may have temporarily released the lock.
  auto establish_invariants = [this, start, end]() TA_REQ(lock_) {
    if (end > size_) {
      return ZX_ERR_BAD_STATE;
    }
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      return ZX_ERR_BAD_STATE;
    }
    RangeChangeUpdateLocked(start, end - start, RangeChangeOp::Unmap);
    return ZX_OK;
  };

  uint64_t start_page_base = ROUNDDOWN(start, PAGE_SIZE);
  uint64_t end_page_base = ROUNDDOWN(end, PAGE_SIZE);

  if (unlikely(start_page_base != start)) {
    // Need to handle the case were end is unaligned and on the same page as start
    if (unlikely(start_page_base == end_page_base)) {
      return ZeroPartialPage(start_page_base, start - start_page_base, end - start_page_base,
                             guard);
    }
    zx_status_t status =
        ZeroPartialPage(start_page_base, start - start_page_base, PAGE_SIZE, guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    start = start_page_base + PAGE_SIZE;
  }

  if (unlikely(end_page_base != end)) {
    zx_status_t status = ZeroPartialPage(end_page_base, 0, end - end_page_base, guard);
    if (status == ZX_OK) {
      status = establish_invariants();
    }
    if (status != ZX_OK) {
      return status;
    }
    end = end_page_base;
  }

  // Now that we have a page aligned range we can try and do the more efficient decommit. We prefer
  // decommit as it performs work in the order of the number of committed pages, instead of work in
  // the order of size of the range. An error from DecommitRangeLocked indicates that the VMO is not
  // of a form that decommit can safely be performed without exposing data that we shouldn't between
  // children and parents, but no actual state will have been changed.
  // Should decommit succeed we are done, otherwise we will have to handle each offset individually.
  zx_status_t status = DecommitRangeLocked(start, end - start, *free_list);
  if (status == ZX_OK) {
    return ZX_OK;
  }

  for (offset = start; offset < end; offset += PAGE_SIZE) {
    VmPageOrMarker* slot = page_list_.Lookup(offset);

    const bool can_see_parent = parent_ && offset < parent_limit_;

    // This is a lambda as it only makes sense to talk about parent mutability when we have a parent
    // for this offset.
    auto parent_immutable = [can_see_parent, this]() TA_REQ(lock_) {
      DEBUG_ASSERT(can_see_parent);
      return parent_->is_hidden();
    };

    // Finding the initial page content is expensive, but we only need to call it
    // under certain circumstances scattered in the code below. The lambda
    // get_initial_page_content() will lazily fetch and cache the details. This
    // avoids us calling it when we don't need to, or calling it more than once.
    struct InitialPageContent {
      bool inited = false;
      VmObjectPaged* page_owner;
      uint64_t owner_offset;
      uint64_t owner_id;
      vm_page_t* page;
    } initial_content_;
    auto get_initial_page_content = [&initial_content_, can_see_parent, this, offset]()
                                        TA_REQ(lock_) -> const InitialPageContent& {
      if (!initial_content_.inited) {
        DEBUG_ASSERT(can_see_parent);
        VmPageOrMarker* page_or_marker = FindInitialPageContentLocked(
            offset, VMM_PF_FLAG_WRITE, &initial_content_.page_owner, &initial_content_.owner_offset,
            &initial_content_.owner_id);
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
    auto free_any_pages = [&free_list] {
      if (!list_is_empty(free_list)) {
        pmm_free(free_list);
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
        list_add_tail(free_list, &page->queue_node);
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
      // Do not pass our free_list here as this takes a list to allocate from, where as our list is
      // for collecting things to free.
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
    if (slot->IsEmpty() && content.page_owner->is_hidden()) {
      free_any_pages();
      zx_status_t result = CloneCowPageAsZeroLocked(offset, free_list, content.page_owner,
                                                    content.page, content.owner_offset);
      if (result != ZX_OK) {
        return result;
      }
      continue;
    }

    // Remove any page that could be hanging around in the slot before we make it a marker.
    if (slot->IsPage()) {
      vm_page_t* page = slot->ReleasePage();
      pmm_page_queues()->Remove(page);
      DEBUG_ASSERT(!list_in_list(&page->queue_node));
      list_add_tail(free_list, &page->queue_node);
    }
    *slot = VmPageOrMarker::Marker();
  }

  return ZX_OK;
}

void VmObjectPaged::MoveToNotWired(vm_page_t* page, uint64_t offset) {
  if (page_source_) {
    pmm_page_queues()->MoveToPagerBacked(page, this, offset);
  } else {
    pmm_page_queues()->MoveToUnswappable(page);
  }
}

void VmObjectPaged::SetNotWired(vm_page_t* page, uint64_t offset) {
  if (page_source_) {
    pmm_page_queues()->SetPagerBacked(page, this, offset);
  } else {
    pmm_page_queues()->SetUnswappable(page);
  }
}

void VmObjectPaged::UnpinPage(vm_page_t* page, uint64_t offset) {
  DEBUG_ASSERT(page->state() == VM_PAGE_STATE_OBJECT);
  ASSERT(page->object.pin_count > 0);
  page->object.pin_count--;
  if (page->object.pin_count == 0) {
    MoveToNotWired(page, offset);
  }
}

void VmObjectPaged::Unpin(uint64_t offset, uint64_t len) {
  Guard<Mutex> guard{&lock_};
  UnpinLocked(offset, len);
}

void VmObjectPaged::UnpinLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // verify that the range is within the object
  ASSERT(InRange(offset, len, size_));
  // forbid zero length unpins as zero length pins return errors.
  ASSERT(len != 0);

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    AssertHeld(parent->lock_);
    return parent->UnpinLocked(offset + parent_offset, len);
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [this](const auto& page, uint64_t off) {
        if (page.IsMarker()) {
          return ZX_ERR_NOT_FOUND;
        }
        UnpinPage(page.Page(), off);
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

bool VmObjectPaged::AnyPagesPinnedLocked(uint64_t offset, size_t len) {
  canary_.Assert();
  DEBUG_ASSERT(lock_.lock().IsHeld());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  if (pinned_page_count_ == 0) {
    return is_contiguous();
  }

  const uint64_t start_page_offset = offset;
  const uint64_t end_page_offset = offset + len;

  bool found_pinned = false;
  page_list_.ForEveryPageInRange(
      [&found_pinned, start_page_offset, end_page_offset](const auto& p, uint64_t off) {
        DEBUG_ASSERT(off >= start_page_offset && off < end_page_offset);
        if (p.IsPage() && p.Page()->object.pin_count > 0) {
          found_pinned = true;
          return ZX_ERR_STOP;
        }
        return ZX_ERR_NEXT;
      },
      start_page_offset, end_page_offset);

  return found_pinned;
}

// Helper function which processes the region visible by both children.
void VmObjectPaged::ReleaseCowParentPagesLockedHelper(uint64_t start, uint64_t end,
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
    // the partial_cow_release_ flag set to prevent fast merge issues in ::RemoveChild.
    auto cur = this;
    AssertHeld(cur->lock_);
    uint64_t cur_start = start;
    uint64_t cur_end = end;
    while (cur->parent_ && cur_start < cur_end) {
      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
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
  VmObjectPaged* parent = VmObjectPaged::AsVmObjectPaged(parent_);
  AssertHeld(parent->lock_);
  parent->page_list_.RemovePages(
      [skip_split_bits, sibling_visible, page_remover, left = this == &parent->left_child_locked()](
          VmPageOrMarker* page_or_mark, uint64_t offset) {
        if (page_or_mark->IsMarker()) {
          // If this marker is in a range still visible to the sibling then we just leave it, no
          // split bits or anything to be updated. If the sibling cannot see it, then we can clear
          // it.
          if (!sibling_visible) {
            *page_or_mark = VmPageOrMarker::Empty();
          }
          return;
        }
        vm_page* page = page_or_mark->Page();
        // If the sibling can still see this page then we need to keep it around, otherwise we can
        // free it. The sibling can see the page if this range is |sibling_visible| and if the
        // sibling hasn't already forked the page, which is recorded in the split bits.
        if (!sibling_visible || left ? page->object.cow_right_split : page->object.cow_left_split) {
          page = page_or_mark->ReleasePage();
          page_remover->Push(page);
          return;
        }
        if (skip_split_bits) {
          // If we were able to update this vmo's parent limit, that made the pages
          // uniaccessible. We clear the split bits to allow ::RemoveChild to efficiently
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
      },
      parent_range_start, parent_range_end);
}

void VmObjectPaged::ReleaseCowParentPagesLocked(uint64_t start, uint64_t end,
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
    VmObjectPaged* cur = this;

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
      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
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

zx_status_t VmObjectPaged::Resize(uint64_t s) {
  canary_.Assert();

  LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

  if (!(options_ & kResizable)) {
    return ZX_ERR_UNAVAILABLE;
  }

  // round up the size to the next page size boundary and make sure we dont wrap
  zx_status_t status = RoundSize(s, &s);
  if (status != ZX_OK) {
    return status;
  }

  Guard<Mutex> guard{&lock_};

  // make sure everything is aligned before we get started
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(s));

  list_node_t free_list;
  list_initialize(&free_list);

  BatchPQRemove page_remover(&free_list);

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
          [](const auto& p, uint64_t off) { return ZX_ERR_NEXT; },
          [&](uint64_t gap_start, uint64_t gap_end) {
            page_source_->OnPagesSupplied(gap_start, gap_end);
            return ZX_ERR_NEXT;
          },
          start, end);
      DEBUG_ASSERT(status == ZX_OK);
    }

    if (parent_ && parent_->is_hidden()) {
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
  guard.Release();
  pmm_free(&free_list);

  return ZX_OK;
}

void VmObjectPaged::UpdateChildParentLimitsLocked(uint64_t new_size) {
  // Note that a child's parent_limit_ will limit that child's descendants' views into
  // this vmo, so this method only needs to touch the direct children.
  for (auto& c : children_list_) {
    DEBUG_ASSERT(c.is_paged());
    VmObjectPaged& child = static_cast<VmObjectPaged&>(c);
    AssertHeld(child.lock_);
    if (new_size < child.parent_offset_) {
      child.parent_limit_ = 0;
    } else {
      child.parent_limit_ = ktl::min(child.parent_limit_, new_size - child.parent_offset_);
    }
  }
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
    if (end_offset > size_) {
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

zx_status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                                  void* context) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  // verify that the range is within the object
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [lookup_fn, context, start_page_offset](const auto& p, uint64_t off) {
        if (p.IsMarker()) {
          return ZX_ERR_NO_MEMORY;
        }
        const size_t index = (off - start_page_offset) / PAGE_SIZE;
        paddr_t pa = p.Page()->paddr();
        zx_status_t status = lookup_fn(context, off, index, pa);
        if (status != ZX_OK) {
          if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
            status = ZX_ERR_INTERNAL;
          }
          return status;
        }
        return ZX_ERR_NEXT;
      },
      [this, lookup_fn, context, start_page_offset](uint64_t gap_start, uint64_t gap_end) {
        AssertHeld(this->lock_);
        // If some page was missing from our list, run the more expensive
        // GetPageLocked to see if our parent has it.
        for (uint64_t off = gap_start; off < gap_end; off += PAGE_SIZE) {
          paddr_t pa;
          zx_status_t status = this->GetPageLocked(off, 0, nullptr, nullptr, nullptr, &pa);
          if (status != ZX_OK) {
            return ZX_ERR_NO_MEMORY;
          }
          const size_t index = (off - start_page_offset) / PAGE_SIZE;
          status = lookup_fn(context, off, index, pa);
          if (status != ZX_OK) {
            if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
              status = ZX_ERR_INTERNAL;
            }
            return status;
          }
        }
        return ZX_ERR_NEXT;
      },
      start_page_offset, end_page_offset);
  if (status != ZX_OK) {
    return status;
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
      guard->CallUnlocked([& info = *copy_result.fault_info, &result, current_aspace] {
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
      guard->CallUnlocked([& info = *copy_result.fault_info, &result, current_aspace] {
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
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  Guard<Mutex> src_guard{&lock_};

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
  if (mapping_list_len_ || children_list_len_) {
    return ZX_ERR_BAD_STATE;
  }

  page_list_.ForEveryPageInRange(
      [](const auto& p, uint64_t off) {
        if (p.IsPage()) {
          pmm_page_queues()->Remove(p.Page());
        }
        return ZX_ERR_NEXT;
      },
      offset, offset + len);

  *pages = page_list_.TakePages(offset, len);

  return ZX_OK;
}

zx_status_t VmObjectPaged::SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  Guard<Mutex> guard{&lock_};
  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint64_t end = offset + len;

  list_node free_list;
  list_initialize(&free_list);

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

    status = AddPageLocked(&src_page, offset);
    if (status == ZX_OK) {
      new_pages_len += PAGE_SIZE;
    } else {
      if (src_page.IsPage()) {
        vm_page_t* page = src_page.ReleasePage();
        DEBUG_ASSERT(!list_in_list(&page->queue_node));
        list_add_tail(&free_list, &page->queue_node);
      }

      if (likely(status == ZX_ERR_ALREADY_EXISTS)) {
        status = ZX_OK;

        // We hit the end of a run of absent pages, so notify the pager source
        // of any new pages that were added and reset the tracking variables.
        if (new_pages_len) {
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
    page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
  }

  if (!list_is_empty(&free_list)) {
    pmm_free(&free_list);
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
zx_status_t VmObjectPaged::FailPageRequests(uint64_t offset, uint64_t len,
                                            zx_status_t error_status) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

  // |error_status| must have already been validated by the PagerDispatcher.
  DEBUG_ASSERT(PageSource::IsValidFailureCode(error_status));

  Guard<Mutex> guard{&lock_};
  ASSERT(page_source_);

  if (!InRange(offset, len, size_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  page_source_->OnPagesFailed(offset, len, error_status);
  return ZX_OK;
}

uint32_t VmObjectPaged::GetMappingCachePolicy() const {
  Guard<Mutex> guard{&lock_};

  return cache_policy_;
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
  if (!page_list_.IsEmpty() && cache_policy_ != ARCH_MMU_FLAG_CACHED) {
    // We forbid to transitioning committed pages from any kind of uncached->cached policy as we do
    // not currently have a story for dealing with the speculative loads that may have happened
    // against the cached physmap. That is, whilst a page was uncached the cached physmap version
    // may have been loaded and sitting in cache. If we switch to cached mappings we may then use
    // stale data out of the cache.
    // This isn't a problem if going *from* an cached state, as we can safely clean+invalidate.
    // Similarly it's not a problem if there aren't actually any committed pages.
    return ZX_ERR_BAD_STATE;
  }
  if (pinned_page_count_ > 0) {
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
    page_list_.ForEveryPage([](const auto& p, uint64_t off) {
      if (p.IsPage()) {
        vm_page_t* page = p.Page();
        arch_clean_invalidate_cache_range((vaddr_t)paddr_to_physmap(page->paddr()), PAGE_SIZE);
      }
      return ZX_ERR_NEXT;
    });
  }

  cache_policy_ = cache_policy;

  return ZX_OK;
}

void VmObjectPaged::RangeChangeUpdateFromParentLocked(const uint64_t offset, const uint64_t len,
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

fbl::RefPtr<PageSource> VmObjectPaged::GetRootPageSourceLocked() const {
  auto vm_object = this;
  AssertHeld(vm_object->lock_);
  while (vm_object->parent_) {
    vm_object = VmObjectPaged::AsVmObjectPaged(vm_object->parent_);
    if (!vm_object) {
      return nullptr;
    }
  }
  return vm_object->page_source_;
}

bool VmObjectPaged::IsCowClonableLocked() const {
  // Copy-on-write clones of pager vmos aren't supported as we can't
  // efficiently make an immutable snapshot.
  if (page_source_) {
    return false;
  }

  // Copy-on-write clones of slices aren't supported at the moment due to the resulting VMO chains
  // having non hidden VMOs between hidden VMOs. This case cannot be handled be CloneCowPageLocked
  // at the moment and so we forbid the construction of such cases for the moment.
  // Bug: 36841
  if (is_slice()) {
    return false;
  }

  // vmos descended from paged/physical vmos can't be eager cloned.
  auto parent = parent_;
  while (parent) {
    auto p = VmObjectPaged::AsVmObjectPaged(parent);
    if (!p || p->page_source_) {
      return false;
    }
    AssertHeld(p->lock_);
    parent = p->parent_;
  }
  return true;
}

VmObjectPaged* VmObjectPaged::PagedParentOfSliceLocked(uint64_t* offset) {
  DEBUG_ASSERT(is_slice());
  VmObjectPaged* cur = this;
  uint64_t off = 0;
  while (cur->is_slice()) {
    AssertHeld(cur->lock_);
    off += cur->parent_offset_;
    DEBUG_ASSERT(cur->parent_);
    DEBUG_ASSERT(cur->parent_->is_paged());
    cur = static_cast<VmObjectPaged*>(cur->parent_.get());
  }
  *offset = off;
  return cur;
}

bool VmObjectPaged::EvictPage(vm_page_t* page, uint64_t offset) {
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

  // |page| is now owned by the caller.
  return true;
}

bool VmObjectPaged::DebugValidatePageSplitsLocked() const {
  if (!is_hidden()) {
    // Nothing to validate on a leaf vmo.
    return true;
  }
  // Assume this is valid until we prove otherwise.
  bool valid = true;
  page_list_.ForEveryPage([this, &valid](const VmPageOrMarker& page, uint64_t offset) {
    if (!page.IsPage()) {
      return ZX_ERR_NEXT;
    }
    vm_page_t* p = page.Page();
    AssertHeld(this->lock_);
    // We found a page in the hidden VMO, if it has been forked in either direction then we
    // expect that if we search down that path we will find that the forked page and that no
    // descendant can 'see' back to this page.
    const VmObjectPaged* expected = nullptr;
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
    const VmObjectPaged* cur = expected;
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
      } else if (cur->is_hidden()) {
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
        VmObjectPaged* next = VmObjectPaged::AsVmObjectPaged(cur->parent_);
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
