// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_object_paged.h"

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
#include <ktl/move.h>
#include <vm/bootreserve.h>
#include <vm/fault.h>
#include <vm/page_source.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

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

}  // namespace

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
  DEBUG_ASSERT(lock_.lock().IsHeld());
  DEBUG_ASSERT(parent_ == nullptr);
  DEBUG_ASSERT(original_parent_user_id_ == 0);

  if (parent->is_paged()) {
    page_list_.InitializeSkew(VmObjectPaged::AsVmObjectPaged(parent)->page_list_.GetSkew(), offset);
  }

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
    Guard<fbl::Mutex> guard{&lock_};
    if (parent_) {
      LTRACEF("removing ourself from our parent %p\n", parent_.get());
      parent_->RemoveChild(this, guard.take());
    }
  } else {
    // Most of the hidden vmo's state should have already been cleaned up when it merged
    // itself into its child in ::OnChildRemoved.
    DEBUG_ASSERT(children_list_len_ == 0);
    DEBUG_ASSERT(page_list_.HasNoPages());
  }

  page_list_.ForEveryPage([this](const auto& p, uint64_t off) {
    if (p.IsPage()) {
      if (this->is_contiguous()) {
        p.Page()->object.pin_count--;
      }
      ASSERT(p.Page()->object.pin_count == 0);
    }
    return ZX_ERR_NEXT;
  });

  list_node_t list;
  list_initialize(&list);

  // free all of the pages attached to us
  page_list_.RemoveAllPages(&list);

  if (page_source_) {
    page_source_->Close();
  }

  pmm_free(&list);
}

uint32_t VmObjectPaged::ScanForZeroPages(bool reclaim) TA_NO_THREAD_SAFETY_ANALYSIS {
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
      typed_child.RangeChangeUpdateLocked(0, typed_child.size(), RangeChangeOp::RemoveWrite);
    }
  }

  uint32_t count = 0;
  page_list_.ForEveryPage(
      [&count, &free_list, reclaim, this](auto& p, uint64_t off) TA_NO_THREAD_SAFETY_ANALYSIS {
        // Pinned pages cannot be decommitted so do not consider them.
        if (p.IsPage() && p.Page()->object.pin_count == 0 && IsZeroPage(p.Page())) {
          count++;
          if (reclaim) {
            // Need to remove all mappings (include read) ones to this range before we remove the
            // page.
            RangeChangeUpdateLocked(off, PAGE_SIZE, RangeChangeOp::Unmap);
            list_add_tail(&free_list, &p.ReleasePage()->queue_node);
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
  for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
    // We don't need thread-safety analysis here, since this VMO has not
    // been shared anywhere yet.
    VmPageOrMarker* slot = [&vmop, &off]() TA_NO_THREAD_SAFETY_ANALYSIS {
      return vmop->page_list_.LookupOrAllocate(off);
    }();
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
    p->object.pin_count++;

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
  }

  if (exclusive && !is_physmap_addr(data)) {
    // unmap it from the kernel
    // NOTE: this means the image can no longer be referenced from original pointer
    status = VmAspace::kernel_aspace()->arch_aspace().Unmap(reinterpret_cast<vaddr_t>(data),
                                                            size / PAGE_SIZE, nullptr);
    ASSERT(status == ZX_OK);
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
  // Insert the new VmObject |hidden_parent| between between |this| and |parent_|.
  if (parent_) {
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

  // Move everything into the hidden parent, for immutability
  hidden_parent->page_list_ = ktl::move(page_list_);
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
    Guard<fbl::Mutex> guard{&lock_};
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
    Guard<fbl::Mutex> guard{&lock_};

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
  switch (type) {
    case CloneType::CopyOnWrite: {
      // To create a copy-on-write clone, the kernel creates an artifical parent vmo
      // called a 'hidden vmo'. The content of the original vmo is moved into the hidden
      // vmo, and the original vmo becomes a child of the hidden vmo. Then a second child
      // is created, which is the userspace visible clone.
      //
      // Hidden vmos are an implementation detail that are not exposed to userspace.

      if (!IsCowClonable()) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      // If this is non-zero, that means that there are pages which hardware can
      // touch, so the vmo can't be safely cloned.
      // TODO: consider immediately forking these pages.
      if (pinned_page_count_) {
        return ZX_ERR_BAD_STATE;
      }

      uint32_t options = kHidden;
      if (is_contiguous()) {
        options |= kContiguous;
      }

      // The initial size is 0. It will be initialized as part of the atomic
      // insertion into the child tree.
      hidden_parent = fbl::AdoptRef<VmObjectPaged>(
          new (&ac) VmObjectPaged(options, pmm_alloc_flags_, 0, lock_ptr_, nullptr));
      if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
      }
      break;
    }
    case CloneType::PrivatePagerCopy:
      if (!GetRootPageSourceLocked()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      break;
  }

  bool notify_one_child;
  {
    Guard<fbl::Mutex> guard{&lock_};

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
      vmo->parent_limit_ = fbl::min(size, size_ - offset);
    }

    VmObjectPaged* clone_parent;
    if (type == CloneType::CopyOnWrite) {
      clone_parent = hidden_parent.get();

      InsertHiddenParentLocked(ktl::move(hidden_parent));

      // Invalidate everything the clone will be able to see. They're COW pages now,
      // so any existing mappings can no longer directly write to the pages.
      RangeChangeUpdateLocked(vmo->parent_offset_, vmo->parent_limit_, RangeChangeOp::RemoveWrite);
    } else {
      clone_parent = this;
    }

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

void VmObjectPaged::RemoveChild(VmObject* removed, Guard<fbl::Mutex>&& adopt) {
  if (!is_hidden()) {
    VmObject::RemoveChild(removed, adopt.take());
    return;
  }

  DEBUG_ASSERT(adopt.wraps_lock(lock_ptr_->lock.lock()));
  Guard<fbl::Mutex> guard{AdoptLock, ktl::move(adopt)};

  // Hidden vmos always have 0 or 2 children, but we can't be here with 0 children.
  DEBUG_ASSERT(children_list_len_ == 2);
  // A hidden vmo must be fully initialized to have 2 children.
  DEBUG_ASSERT(user_id_ != ZX_KOID_INVALID);
  bool removed_left = &left_child_locked() == removed;

  DropChildLocked(removed);
  DEBUG_ASSERT(children_list_.front().is_paged());
  VmObjectPaged& child = static_cast<VmObjectPaged&>(children_list_.front());

  // Merge this vmo's content into the remaining child.
  DEBUG_ASSERT(removed->is_paged());
  MergeContentWithChildLocked(static_cast<VmObjectPaged*>(removed), removed_left);

  // The child which removed itself and led to the invocation should have a reference
  // to us, in addition to child.parent_ which we are about to clear.
  DEBUG_ASSERT(ref_count_debug() >= 2);

  if (child.page_attribution_user_id_ != page_attribution_user_id_) {
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
    uint64_t user_id_to_skip = page_attribution_user_id_;
    while (cur->parent_ != nullptr) {
      DEBUG_ASSERT(cur->parent_->is_hidden());
      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);

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
  DropChildLocked(&child);
  if (parent_) {
    parent_->ReplaceChildLocked(this, &child);
  }
  child.parent_ = ktl::move(parent_);

  // We need to proxy the closure down to the original user-visible vmo. To find
  // that, we can walk down the clone tree following the user_id_.
  VmObjectPaged* descendant = &child;
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

void VmObjectPaged::MergeContentWithChildLocked(VmObjectPaged* removed, bool removed_left) {
  DEBUG_ASSERT(children_list_len_ == 1);
  DEBUG_ASSERT(children_list_.front().is_paged());
  VmObjectPaged& child = static_cast<VmObjectPaged&>(children_list_.front());

  list_node freed_pages;
  list_initialize(&freed_pages);

  const uint64_t visibility_start_offset = child.parent_offset_ + child.parent_start_limit_;
  const uint64_t merge_start_offset = child.parent_offset_;
  const uint64_t merge_end_offset = child.parent_offset_ + child.parent_limit_;

  page_list_.RemovePages(0, visibility_start_offset, &freed_pages);
  page_list_.RemovePages(merge_end_offset, MAX_SIZE, &freed_pages);

  if (child.parent_offset_ + child.parent_limit_ > parent_limit_) {
    // Update the child's parent limit to ensure that it won't be able to see more
    // of its new parent than this hidden vmo was able to see.
    if (parent_limit_ < child.parent_offset_) {
      child.parent_limit_ = 0;
      child.parent_start_limit_ = 0;
    } else {
      child.parent_limit_ = parent_limit_ - child.parent_offset_;
      child.parent_start_limit_ = fbl::min(child.parent_start_limit_, child.parent_limit_);
    }
  } else {
    // The child will be able to see less of its new parent than this hidden vmo was
    // able to see, so release any parent pages in that range.
    ReleaseCowParentPagesLocked(merge_end_offset, parent_limit_, &freed_pages);
  }

  if (removed->parent_offset_ + removed->parent_start_limit_ < visibility_start_offset) {
    // If the removed former child has a smaller offset, then there are retained
    // ancestor pages that will no longer be visible and thus should be freed.
    ReleaseCowParentPagesLocked(removed->parent_offset_ + removed->parent_start_limit_,
                                visibility_start_offset, &freed_pages);
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

  if (is_contiguous()) {
    vm_page_t* p;
    list_for_every_entry (&freed_pages, p, vm_page_t, queue_node) {
      // The pages that have been freed all come from contigous hidden vmos, so they can
      // either be contiguously pinned or have been migrated into their other child.
      DEBUG_ASSERT(p->object.pin_count <= 1);
      p->object.pin_count = 0;
    }
  }

  // At this point, we need to merge |this|'s page list and |child|'s page list.
  //
  // In general, COW clones are expected to share most of their pages (i.e. to fork a relatively
  // small number of pages). Because of this, it is preferable to do work proportional to the
  // number of pages which were forked into |removed|. However, there are a few things that can
  // prevent this:
  //   - If |child|'s offset is non-zero then the offsets of all of |this|'s pages will
  //     need to be updated when they are merged into |child|.
  //   - If |this| is contiguous and |child| is not, then all of |this|'s page's pin
  //     counts will need to be updated when they are migrated into |child|.
  //   - If there has been a call to ReleaseCowParentPagesLocked which was not able to
  //     update the parent limits, then there can exist pages in this vmo's page list
  //     which are not visible to |child| but can't be easily freed based on its parent
  //     limits. Finding these pages requires examining the split bits of all pages.
  //   - If |child| is hidden, then there can exist pages in this vmo which were split into
  //     |child|'s subtree and then migrated out of |child|. Those pages need to be freed, and
  //     the simplest way to find those pages is to examine the split bits.
  bool fast_merge = merge_start_offset == 0 && !(is_contiguous() && !child.is_contiguous()) &&
                    !partial_cow_release_ && !child.is_hidden();

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

    // Now merge |child|'s pages into |this|, overwriting any pages present in |this|, and
    // then move that list to |child|.
    child.page_list_.MergeOnto(page_list_, &covered_pages);
    child.page_list_ = ktl::move(page_list_);

#ifdef DEBUG_ASSERT_IMPLEMENTED
    vm_page_t* p;
    list_for_every_entry (&covered_pages, p, vm_page_t, queue_node) {
      // The page was already present in |child|, so it should be split at least
      // once. And being split twice is obviously bad.
      ASSERT(p->object.cow_left_split ^ p->object.cow_right_split);
      // If |this| is contig, then we're only here if |child| is also contig. In that
      // case, any covered pages must be covered by the original contig page in |child|
      // and must be unpinned themselves.
      ASSERT(p->object.pin_count == 0);
    }
#endif
    list_splice_after(&covered_pages, &freed_pages);
  } else {
    // Merge our page list into the child page list and update all the necessary metadata.
    child.page_list_.MergeFrom(
        page_list_, merge_start_offset, merge_end_offset,
        [this_is_contig = this->is_contiguous()](vm_page* page, uint64_t offset) {
          if (this_is_contig) {
            // If this vmo is contiguous, unpin the pages that aren't needed. The pages
            // are either original contig pages (which should have a pin_count of 1),
            // or they're forked pages where the original is already in the contig
            // child (in which case pin_count should be 0).
            DEBUG_ASSERT(page->object.pin_count <= 1);
            page->object.pin_count = 0;
          }
        },
        [this_is_contig = this->is_contiguous(), child_is_contig = child.is_contiguous(),
         removed_left](vm_page* page, uint64_t offset) -> bool {
          if (child_is_contig) {
            // We moved the page into the contiguous vmo, so we expect the page
            // to be a pinned contiguous page.
            DEBUG_ASSERT(page->object.pin_count == 1);
          } else if (this_is_contig) {
            // This vmo was contiguous but the child isn't, so unpin the pages. Similar
            // to above, this should be at most 1.
            DEBUG_ASSERT(page->object.pin_count <= 1);
            page->object.pin_count = 0;
          } else {
            // Neither is contiguous, so the page shouldn't have been pinned.
            DEBUG_ASSERT(page->object.pin_count == 0);
          }

          if (removed_left ? page->object.cow_right_split : page->object.cow_left_split) {
            // This happens when the pages was already migrated into child but then
            // was migrated further into child's descendants. The page can be freed.
            return false;
          } else {
            // Since we recursively fork on write, if the child doesn't have the
            // page, then neither of its children do.
            page->object.cow_left_split = 0;
            page->object.cow_right_split = 0;
            return true;
          }
        },
        &freed_pages);
  }

  if (!list_is_empty(&freed_pages)) {
    pmm_free(&freed_pages);
  }
}

void VmObjectPaged::Dump(uint depth, bool verbose) {
  canary_.Assert();

  // This can grab our lock.
  uint64_t parent_id = parent_user_id();

  Guard<fbl::Mutex> guard{&lock_};

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
  printf("vmo %p/k%" PRIu64 " size %#" PRIx64 " offset %#" PRIx64 " limit %#" PRIx64
         " pages %zu ref %d parent %p/k%" PRIu64 "\n",
         this, user_id_, size_, parent_offset_, parent_limit_, count, ref_count_debug(),
         parent_.get(), parent_id);

  if (verbose) {
    auto f = [depth](const auto& p, uint64_t offset) {
      for (uint i = 0; i < depth + 1; ++i) {
        printf("  ");
      }
      if (p.IsMarker()) {
        printf("offset %#" PRIx64 " zero page marker\n", offset);
      } else {
        printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "\n", offset, p.Page(),
               p.Page()->paddr());
      }
      return ZX_ERR_NEXT;
    };
    page_list_.ForEveryPage(f);
  }
}

size_t VmObjectPaged::AttributedPagesInRange(uint64_t offset, uint64_t len) const {
  canary_.Assert();
  Guard<fbl::Mutex> guard{&lock_};
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
    const uint64_t parent_size = fbl::min(cur_size, cur->parent_limit_ - cur_offset);

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
  Guard<fbl::Mutex> guard{&lock_};

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
  *page = ktl::move(*p);

  if (do_range_update) {
    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE, RangeChangeOp::Unmap);
  }

  return ZX_OK;
}

bool VmObjectPaged::IsUniAccessibleLocked(vm_page_t* page, uint64_t offset) const {
  DEBUG_ASSERT(lock_.lock().IsHeld());
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
    VmObjectPaged* next = VmObjectPaged::AsVmObjectPaged(cur->parent_);
    // We can't make COW clones of physical vmos, so this can only happen if we
    // somehow don't find |page_owner| in the ancestor chain.
    DEBUG_ASSERT(next);

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
  VmObjectPaged* last_contig = nullptr;
  uint64_t last_contig_offset = 0;

  bool alloc_failure = false;

  // As long as we're simply migrating |page|, there's no need to update any vmo mappings, since
  // that means the other side of the clone tree has already covered |page| and the current side
  // of the clone tree will still see |page|. As soon as we insert a new page, we'll need to
  // update all mappings at or below that level.
  bool skip_range_update = true;
  do {
    // |target_page| is always located at in |cur| at |cur_offset| at the start of the loop.
    VmObjectPaged* target_page_owner = cur;
    uint64_t target_page_offset = cur_offset;

    cur = cur->stack_.dir_flag == StackDir::Left ? &cur->left_child_locked()
                                                 : &cur->right_child_locked();
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

      // To maintain the contiguity of the user-visible vmo, keep track of the
      // leaf-most contiguous vmo that has a page inserted into it. We'll come
      // back later and make sure this vmo sees the original contiguous page.
      if (cur->is_contiguous()) {
        last_contig = cur;
        last_contig_offset = cur_offset;
      }

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

  if (last_contig != nullptr) {
    ContiguousCowFixupLocked(page_owner, owner_offset, last_contig, last_contig_offset);
    if (last_contig == this) {
      target_page = page;
    }
  }

  if (unlikely(alloc_failure)) {
    // Note that this happens after fixing up the contiguous vmo invariant.
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
    page = AsVmObjectPaged(parent_)->CloneCowPageLocked(offset + parent_offset_, free_list,
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

void VmObjectPaged::ContiguousCowFixupLocked(VmObjectPaged* page_owner, uint64_t page_owner_offset,
                                             VmObjectPaged* last_contig,
                                             uint64_t last_contig_offset) {
  // If we're here, then |last_contig| must be contiguous, and all of its
  // ancestors (including |page_owner|) must be contiguous.
  DEBUG_ASSERT(last_contig->is_contiguous());
  DEBUG_ASSERT(page_owner->is_contiguous());

  // When this function is invoked, we know that the desired contiguous page is somewhere
  // between |page_owner| and |last_contig|. Since ::CloneCowPageLocked will no longer
  // migrate the original page once it forks that page, we know that the desired contiguous
  // page is in the root-most vmo that has a page corresponding to the offset.
  //
  // In other words, we can start searching from |page_owner| and progress towards the
  // leaf vmo, and the first page that is found will be the page that needs to be moved
  // into |last_contig|.

  // Use ::ForEveryPageInRange so that we can directly swap the vm_page_t entries
  // in the page lists without having to worry about allocation.
  bool found = false;
  last_contig->page_list_.ForEveryPageInRange(
      [page_owner, page_owner_offset, last_contig, &found](VmPageOrMarker& page1, uint64_t off) {
        if (page1.IsMarker()) {
          return ZX_ERR_NEXT;
        }
        auto swap_fn = [&page1, &found](VmPageOrMarker& page2, uint64_t off) {
          if (page2.IsMarker()) {
            return ZX_ERR_NEXT;
          }
          // We're guaranteed that the first page we see is the one we want.
          DEBUG_ASSERT(page2.Page()->object.pin_count == 1);
          found = true;

          VmPageOrMarker temp = ktl::move(page1);
          page1 = ktl::move(page2);
          page2 = ktl::move(temp);

          bool flag = page1.Page()->object.cow_left_split;
          page1.Page()->object.cow_left_split = page2.Page()->object.cow_left_split;
          page2.Page()->object.cow_left_split = flag;

          flag = page1.Page()->object.cow_right_split;
          page1.Page()->object.cow_right_split = page2.Page()->object.cow_right_split;
          page2.Page()->object.cow_right_split = flag;

          // Don't swap the pin counts, since those are relevant to the
          // actual physical pages, not to what vmo they're contained in.

          return ZX_ERR_NEXT;
        };

        VmObjectPaged* cur = page_owner;
        uint64_t cur_offset = page_owner_offset;
        while (!found && cur != last_contig) {
          AssertHeld(cur->lock_);
          zx_status_t status =
              cur->page_list_.ForEveryPageInRange(swap_fn, cur_offset, cur_offset + PAGE_SIZE);
          DEBUG_ASSERT(status == ZX_OK);

          if (found) {
            cur->RangeChangeUpdateLocked(cur_offset, PAGE_SIZE, RangeChangeOp::Unmap);
          } else {
            cur = cur->stack_.dir_flag == StackDir::Left ? &cur->left_child_locked()
                                                         : &cur->right_child_locked();
            cur_offset = cur_offset - cur->parent_offset_;

            DEBUG_ASSERT(cur->is_contiguous());
          }
        }
        return ZX_ERR_NEXT;
      },
      last_contig_offset, last_contig_offset + PAGE_SIZE);
  DEBUG_ASSERT(found);

  // It's not necessary to invoke ::RangeChangeUpdateLocked on the |last_contig|, as it is a
  // descendant of whatever vmo ::RangeChangeUpdateLocked was invoked when pages were swapped.

  DEBUG_ASSERT(last_contig->page_list_.Lookup(last_contig_offset)->Page()->object.pin_count == 1);
}

vm_page_t* VmObjectPaged::FindInitialPageContentLocked(uint64_t offset, uint pf_flags,
                                                       VmObject** owner_out,
                                                       uint64_t* owner_offset_out) {
  DEBUG_ASSERT(page_list_.Lookup(offset) == nullptr || page_list_.Lookup(offset)->IsEmpty());

  // Search up the clone chain for any committed pages. cur_offset is the offset
  // into cur we care about. The loop terminates either when that offset contains
  // a committed page or when that offset can't reach into the parent.
  vm_page_t* page = nullptr;
  VmObjectPaged* cur = this;
  uint64_t cur_offset = offset;
  while (!page && cur_offset < cur->parent_limit_) {
    // If there's no parent, then parent_limit_ is 0 and we'll never enter the loop
    DEBUG_ASSERT(cur->parent_);

    uint64_t parent_offset;
    bool overflowed = add_overflow(cur->parent_offset_, cur_offset, &parent_offset);
    ASSERT(!overflowed);
    if (parent_offset >= cur->parent_->size()) {
      // The offset is off the end of the parent, so cur is the VmObject
      // which will provide the page.
      break;
    }

    if (!cur->parent_->is_paged()) {
      uint parent_pf_flags = pf_flags & ~VMM_PF_FLAG_WRITE;
      auto status = cur->parent_->GetPageLocked(parent_offset, parent_pf_flags, nullptr, nullptr,
                                                &page, nullptr);
      // The first if statement should ensure we never make an out-of-range query into a
      // physical VMO, and physical VMOs will always return a page for all valid offsets.
      DEBUG_ASSERT(status == ZX_OK);
      DEBUG_ASSERT(page != nullptr);

      *owner_out = cur->parent_.get();
      *owner_offset_out = parent_offset;
      return page;
    } else {
      cur = VmObjectPaged::AsVmObjectPaged(cur->parent_);
      cur_offset = parent_offset;
      VmPageOrMarker* p = cur->page_list_.Lookup(parent_offset);
      if (p && !p->IsEmpty()) {
        // If we found a page we want to return it, and if we found a marker we should stop
        // searching.
        if (p->IsPage()) {
          page = p->Page();
        }
        break;
      }
    }
  }

  *owner_out = cur;
  *owner_offset_out = cur_offset;

  return page;
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
  DEBUG_ASSERT(lock_.lock().IsHeld());
  DEBUG_ASSERT(!is_hidden());

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  offset = ROUNDDOWN(offset, PAGE_SIZE);

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    return parent->GetPageLocked(offset + parent_offset, pf_flags, free_list, page_request,
                                 page_out, pa_out);
  }

  bool is_marker = false;

  {
    // see if we already have a page at that offset.
    VmPageOrMarker* p = page_list_.Lookup(offset);
    if (p) {
      if (p->IsMarker()) {
        is_marker = true;
      } else if (p->IsPage()) {
        if (page_out) {
          *page_out = p->Page();
        }
        if (pa_out) {
          *pa_out = p->Page()->paddr();
        }
        return ZX_OK;
      }
    }
  }

  vm_page* p = nullptr;

  __UNUSED char pf_string[5];
  LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
          vmm_pf_flags_to_string(pf_flags, pf_string));

  VmObject* page_owner;
  uint64_t owner_offset;
  if (!parent_ || is_marker) {
    // Avoid the function call in the common case.
    page_owner = this;
    owner_offset = offset;
  } else {
    p = FindInitialPageContentLocked(offset, pf_flags, &page_owner, &owner_offset);
  }

  if (!p) {
    // If we're not being asked to sw or hw fault in the page, return not found.
    if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    // Since physical VMOs always provide pages for their full range, we should
    // never get here for physical VMOs.
    DEBUG_ASSERT(page_owner->is_paged());
    VmObjectPaged* typed_owner = static_cast<VmObjectPaged*>(page_owner);

    if (typed_owner->page_source_) {
      zx_status_t status =
          typed_owner->page_source_->GetPage(owner_offset, page_request, &p, nullptr);
      // Pager page sources will never synchronously return a page.
      DEBUG_ASSERT(status != ZX_OK);

      if (typed_owner != this && status == ZX_ERR_NOT_FOUND) {
        // The default behavior of clones of detached pager VMOs fault in zero
        // pages instead of propagating the pager's fault.
        // TODO(stevensd): Add an arg to zx_vmo_create_child to optionally fault here.
        p = vm_get_zero_page();
      } else {
        return status;
      }
    } else {
      // If there's no page source, we're using an anonymous page. It's not
      // necessary to fault a writable page directly into the owning VMO.
      p = vm_get_zero_page();
    }
  }
  DEBUG_ASSERT(p);

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

    // If ARM and not fully cached, clean/invalidate the page after zeroing it. Since
    // clones must be cached, we only need to check this here.
#if ARCH_ARM64
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
      arch_clean_invalidate_cache_range((addr_t)paddr_to_physmap(res_page->paddr()), PAGE_SIZE);
    }
#endif
  } else {
    // We need a writable page; let ::CloneCowPageLocked handle inserting one.
    res_page = CloneCowPageLocked(offset, free_list, static_cast<VmObjectPaged*>(page_owner), p,
                                  owner_offset);
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

zx_status_t VmObjectPaged::CommitRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

  Guard<fbl::Mutex> guard{&lock_};

  // trim the size
  uint64_t new_len;
  if (!TrimRange(offset, len, size_, &new_len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // was in range, just zero length
  if (new_len == 0) {
    return ZX_OK;
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

    if (count == 0) {
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

  bool retry = false;
  PageRequest page_request(true);
  do {
    if (retry) {
      // If there was a page request that couldn't be fulfilled, we need wait on the
      // request and retry the commit. Note that when we retry the loop, offset is
      // updated past the portion of the vmo that we successfully commited.
      zx_status_t status = ZX_OK;
      guard.CallUnlocked([&page_request, &status]() mutable { status = page_request.Wait(); });
      if (status != ZX_OK) {
        return status;
      }
      retry = false;

      // Re-run the range checks, since size_ could have changed while we were blocked. This
      // is not a failure, since the arguments were valid when the syscall was made. It's as
      // if the commit was successful but then the pages were thrown away.
      if (!TrimRange(offset, new_len, size_, &new_len)) {
        return ZX_OK;
      }

      if (new_len == 0) {
        return ZX_OK;
      }

      end = ROUNDUP_PAGE_SIZE(offset + new_len);
      DEBUG_ASSERT(end > offset);
      offset = ROUNDDOWN(offset, PAGE_SIZE);
    }

    // cur_offset tracks how far we've made page requests, even if they're not done
    uint64_t cur_offset = offset;
    // new_offset tracks how far we've successfully committed and is where we'll
    // restart from if we need to retry the commit
    uint64_t new_offset = offset;
    while (cur_offset < end) {
      // Don't commit if we already have this page
      VmPageOrMarker* p = page_list_.Lookup(cur_offset);
      if (!p || !p->IsPage()) {
        // Check if our parent has the page
        const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
        zx_status_t res =
            GetPageLocked(cur_offset, flags, &page_list, &page_request, nullptr, nullptr);
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
      }

      cur_offset += PAGE_SIZE;
      if (!retry) {
        new_offset = offset;
      }
    }

    // Unmap all of the pages in the range we touched. This may end up unmapping non-present
    // ranges or unmapping things multiple times, but it's necessary to ensure that we unmap
    // everything that actually is present before anything else sees it.
    if (cur_offset - offset) {
      RangeChangeUpdateLocked(offset, cur_offset - offset, RangeChangeOp::Unmap);
    }

    if (retry && cur_offset == end) {
      zx_status_t res = root_source->FinalizeRequest(&page_request);
      if (res != ZX_ERR_SHOULD_WAIT) {
        return res;
      }
    }
    offset = new_offset;
  } while (retry);

  return ZX_OK;
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);
  list_node_t list;
  list_initialize(&list);
  zx_status_t status;
  {
    Guard<fbl::Mutex> guard{&lock_};
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

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    // Use a lambda to escape thread analysis as it does not understand that we are holding the
    // parents lock right now.
    return [parent, &free_list, len](uint64_t offset) TA_NO_THREAD_SAFETY_ANALYSIS -> zx_status_t {
      return parent->DecommitRangeLocked(offset, len, free_list);
    }(offset + parent_offset);
  }

  if (parent_ || GetRootPageSourceLocked()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Demand offset and length be correctly aligned to not give surprising user semantics.
  if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // trim the size
  uint64_t new_len;
  if (!TrimRange(offset, len, size_, &new_len)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // was in range, just zero length
  if (new_len == 0) {
    return ZX_OK;
  }

  LTRACEF("start offset %#" PRIx64 ", end %#" PRIx64 "\n", offset, offset + new_len);

  // TODO(teisenbe): Allow decommitting of pages pinned by
  // CommitRangeContiguous

  if (AnyPagesPinnedLocked(offset, new_len)) {
    return ZX_ERR_BAD_STATE;
  }

  // unmap all of the pages in this range on all the mapping regions
  RangeChangeUpdateLocked(offset, new_len, RangeChangeOp::Unmap);

  page_list_.RemovePages(offset, offset + new_len, &free_list);

  return ZX_OK;
}

zx_status_t VmObjectPaged::ZeroRange(uint64_t offset, uint64_t len) {
  canary_.Assert();
  list_node_t list;
  list_initialize(&list);
  zx_status_t status;
  {
    Guard<fbl::Mutex> guard{&lock_};
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
                                           uint64_t zero_end_offset, Guard<fbl::Mutex>* guard) {
  DEBUG_ASSERT(zero_start_offset < zero_end_offset);
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
    VmObject* page_owner;
    uint64_t owner_offset;
    if (!FindInitialPageContentLocked(page_base_offset, VMM_PF_FLAG_WRITE, &page_owner,
                                      &owner_offset)) {
      // Parent doesn't have a page either, so nothing to do this is already zero.
      return ZX_OK;
    }
  }
  // Need to actually zero out bytes in the page.
  return ReadWriteInternalLocked(
      page_base_offset + zero_start_offset, zero_end_offset - zero_start_offset, true,
      [](void* dst, size_t offset, size_t len, Guard<fbl::Mutex>* guard) -> zx_status_t {
        // We're memsetting the *kernel* address of an allocated page, so we know that this
        // cannot fault. memset may not be the most efficient, but we don't expect to be doing
        // this very often.
        memset(dst, 0, len);
        return ZX_OK;
      },
      guard);
}

zx_status_t VmObjectPaged::ZeroRangeLocked(uint64_t offset, uint64_t len, list_node_t* free_list,
                                           Guard<fbl::Mutex>* guard) {
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
      ReleaseCowParentPagesLocked(rounded_start, parent_limit_, free_list);
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
      VmObject* page_owner;
      uint64_t owner_offset;
      vm_page_t* page;
    } initial_content_;
    auto get_initial_page_content = [&initial_content_, can_see_parent, this, offset]()
                                        TA_REQ(lock_) -> const InitialPageContent& {
      if (!initial_content_.inited) {
        DEBUG_ASSERT(can_see_parent);
        initial_content_.page =
            FindInitialPageContentLocked(offset, VMM_PF_FLAG_WRITE, &initial_content_.page_owner,
                                         &initial_content_.owner_offset);
        initial_content_.inited = true;
      }
      return initial_content_;
    };

    auto parent_has_content = [get_initial_page_content]() TA_REQ(lock_) {
      return get_initial_page_content().page != nullptr;
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
        list_add_tail(free_list, &page_list_.RemovePage(offset).ReleasePage()->queue_node);
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
      bool result = AllocateCopyPage(pmm_alloc_flags_, vm_get_zero_page_paddr(), free_list, &p);
      if (!result) {
        return ZX_ERR_NO_MEMORY;
      }
      *slot = VmPageOrMarker::Page(p);
      continue;
    }
    DEBUG_ASSERT(parent_ && parent_has_content());

    // We are able to insert a marker, but if our page content is from a hidden owner we need to
    // perform slightly more complex cow forking.
    const InitialPageContent& content = get_initial_page_content();
    if (slot->IsEmpty() && content.page_owner->is_hidden()) {
      // We know it is paged because we just checked that it was hidden.
      VmObjectPaged* typed_owner = static_cast<VmObjectPaged*>(content.page_owner);
      zx_status_t result = CloneCowPageAsZeroLocked(offset, free_list, typed_owner, content.page,
                                                    content.owner_offset);
      if (result != ZX_OK) {
        return result;
      }
      continue;
    }

    // Remove any page that could be hanging around in the slot before we make it a marker.
    if (slot->IsPage()) {
      list_add_tail(free_list, &slot->ReleasePage()->queue_node);
    }
    *slot = VmPageOrMarker::Marker();
  }

  return ZX_OK;
}

zx_status_t VmObjectPaged::Pin(uint64_t offset, uint64_t len) {
  canary_.Assert();

  Guard<fbl::Mutex> guard{&lock_};
  return PinLocked(offset, len);
}

zx_status_t VmObjectPaged::PinLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();

  // verify that the range is within the object
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (unlikely(len == 0)) {
    return ZX_OK;
  }

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    // Use a lambda to escape thread analysis as it does not understand that we are holding the
    // parents lock right now.
    return [parent, len](uint64_t offset) TA_NO_THREAD_SAFETY_ANALYSIS -> zx_status_t {
      return parent->PinLocked(offset, len);
    }(offset + parent_offset);
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  uint64_t pin_range_end = start_page_offset;
  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [&pin_range_end](const auto& page, uint64_t off) {
        if (page.IsMarker()) {
          return ZX_ERR_NOT_FOUND;
        }
        vm_page* p = page.Page();
        DEBUG_ASSERT(p->state() == VM_PAGE_STATE_OBJECT);
        if (p->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
          return ZX_ERR_UNAVAILABLE;
        }

        p->object.pin_count++;
        pin_range_end = off + PAGE_SIZE;
        return ZX_ERR_NEXT;
      },
      [](uint64_t gap_start, uint64_t gap_end) { return ZX_ERR_NOT_FOUND; }, start_page_offset,
      end_page_offset);

  if (status != ZX_OK) {
    pinned_page_count_ += (pin_range_end - start_page_offset) / PAGE_SIZE;
    UnpinLocked(start_page_offset, pin_range_end - start_page_offset);
    return status;
  }

  // Pinning every page in the largest vmo possible as many times as possible can't overflow
  static_assert(VmObjectPaged::MAX_SIZE / PAGE_SIZE < UINT64_MAX / VM_PAGE_OBJECT_MAX_PIN_COUNT);
  pinned_page_count_ += (end_page_offset - start_page_offset) / PAGE_SIZE;

  return ZX_OK;
}

void VmObjectPaged::Unpin(uint64_t offset, uint64_t len) {
  Guard<fbl::Mutex> guard{&lock_};
  UnpinLocked(offset, len);
}

void VmObjectPaged::UnpinLocked(uint64_t offset, uint64_t len) {
  canary_.Assert();
  DEBUG_ASSERT(lock_.lock().IsHeld());

  // verify that the range is within the object
  ASSERT(InRange(offset, len, size_));

  if (unlikely(len == 0)) {
    return;
  }

  if (is_slice()) {
    uint64_t parent_offset;
    VmObjectPaged* parent = PagedParentOfSliceLocked(&parent_offset);
    // Use a lambda to escape thread analysis as it does not understand that we are holding the
    // parents lock right now.
    return [parent, len](uint64_t offset) TA_NO_THREAD_SAFETY_ANALYSIS {
      parent->UnpinLocked(offset, len);
    }(offset + parent_offset);
  }

  const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
  const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

  zx_status_t status = page_list_.ForEveryPageAndGapInRange(
      [](const auto& page, uint64_t off) {
        if (page.IsMarker()) {
          return ZX_ERR_NOT_FOUND;
        }
        vm_page* p = page.Page();
        DEBUG_ASSERT(p->state() == VM_PAGE_STATE_OBJECT);
        ASSERT(p->object.pin_count > 0);
        p->object.pin_count--;
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
                                                      list_node_t* free_list) {
  // Compute the range in the parent that cur no longer will be able to see.
  uint64_t parent_range_start, parent_range_end;
  bool overflow = add_overflow(start, parent_offset_, &parent_range_start);
  bool overflow2 = add_overflow(end, parent_offset_, &parent_range_end);
  DEBUG_ASSERT(!overflow && !overflow2);  // vmo creation should have failed.

  bool skip_split_bits;
  if (parent_limit_ == end) {
    parent_limit_ = start;
    if (parent_limit_ <= parent_start_limit_) {
      // Setting both to zero is cleaner and makes some asserts easier.
      parent_start_limit_ = 0;
      parent_limit_ = 0;
    }
    skip_split_bits = true;
  } else if (start == parent_start_limit_) {
    parent_start_limit_ = end;
    skip_split_bits = true;
  } else {
    // If the vmo limits can't be updated, this function will need to use the split bits
    // to release pages in the parent. It also means that ancestor pages in the specified
    // range might end up being released based on their current split bits, instead of through
    // subsequent calls to this function. Therefore parent and all ancestors need to have
    // the partial_cow_release_ flag set to prevent fast merge issues in ::RemoveChild.
    auto cur = this;
    uint64_t cur_start = start;
    uint64_t cur_end = end;
    while (cur->parent_ && cur_start < cur_end) {
      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
      parent->partial_cow_release_ = true;
      cur_start = fbl::max(cur_start + cur->parent_offset_, parent->parent_start_limit_);
      cur_end = fbl::min(cur_end + cur->parent_offset_, parent->parent_limit_);
      cur = parent;
    }
    skip_split_bits = false;
  }

  // Free any pages that were already split into the other child. For pages that haven't been split
  // into the other child, we need to ensure they're univisible.
  auto parent = VmObjectPaged::AsVmObjectPaged(parent_);
  parent->page_list_.RemovePages(
      [skip_split_bits, left = this == &parent->left_child_locked()](
          const VmPageOrMarker& page_or_mark, auto offset) -> bool {
        if (page_or_mark.IsMarker())
          return true;
        vm_page* page = page_or_mark.Page();
        // Simply checking if the page is resident in |this|->page_list_ is insufficient, as the
        // page split into this vmo could have been migrated anywhere into is children. To avoid
        // having to search its entire child subtree, we need to track into which subtree
        // a page is split (i.e. have two directional split bits instead of a single split bit).
        if (left ? page->object.cow_right_split : page->object.cow_left_split) {
          return true;
        }
        if (skip_split_bits) {
          // If we were able to update this vmo's parent limit, that made the pages
          // uniaccessible. We clear the split bits to allow ::OnChildRemoved to efficiently
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
        return false;
      },
      parent_range_start, parent_range_end, free_list);
}

void VmObjectPaged::ReleaseCowParentPagesLocked(uint64_t root_start, uint64_t root_end,
                                                list_node_t* free_list) {
  // This function releases |root}'s references to any ancestor vmo's COW pages.
  //
  // To do so, we divide |root|'s parent into three (possibly 0-length) regions: the region
  // which |root| sees but before what the sibling can see, the region where both |root|
  // and its sibling can see, and the region |root| can see but after what the sibling can
  // see. Processing the 2nd region only requires touching the direct parent, since the sibling
  // can see ancestor pages in the region. However, processing the 1st and 3rd regions requires
  // recursively releasing |root|'s parent's ancestor pages, since those pages are no longer
  // visible through |root|'s parent.
  //
  // This function processes region 1 (incl. recursively processing the parent), then region 2,
  // then region 3 (incl. recursively processing the parent). To facilitate coming back up the
  // tree, the end offset of the range being processed is stashed in the vmobject. The start
  // offset can be easily recovered as it corresponds to the end value of the recursive region.
  auto cur = this;
  uint64_t cur_start = root_start;
  uint64_t cur_end = root_end;

  do {
    uint64_t start = fbl::max(cur_start, cur->parent_start_limit_);
    uint64_t end = fbl::min(cur_end, cur->parent_limit_);

    // First check to see if the given range in cur even refers to ancestor pages.
    if (start < end && cur->parent_ && cur->parent_start_limit_ != cur->parent_limit_) {
      DEBUG_ASSERT(cur->parent_->is_hidden());

      auto parent = VmObjectPaged::AsVmObjectPaged(cur->parent_);
      bool left = cur == &parent->left_child_locked();
      auto& other = left ? parent->right_child_locked() : parent->left_child_locked();

      // Compute the range in the parent that cur no longer will be able to see.
      uint64_t parent_range_start, parent_range_end;
      bool overflow = add_overflow(start, cur->parent_offset_, &parent_range_start);
      bool overflow2 = add_overflow(end, cur->parent_offset_, &parent_range_end);
      DEBUG_ASSERT(!overflow && !overflow2);  // vmo creation should have failed.

      uint64_t tail_start;
      if (other.parent_start_limit_ != other.parent_limit_) {
        if (parent_range_start < other.parent_offset_ + other.parent_start_limit_) {
          // If there is a region being freed before what the sibling can see,
          // then walk down into the parent. Note that when we come back up the
          // tree to process this vmo, we won't fall into this branch since the
          // start offset will be set to head_end.
          uint64_t head_end =
              fbl::min(other.parent_offset_ + other.parent_start_limit_, parent_range_end);
          parent->page_list_.RemovePages(parent_range_start, head_end, free_list);

          if (start == cur->parent_start_limit_) {
            cur->parent_start_limit_ = head_end;
          }

          DEBUG_ASSERT((cur_end & 1) == 0);  // cur_end is page aligned
          uint64_t scratch = cur_end >> 1;   // gcc is finicky about setting bitfields
          cur->stack_.scratch = scratch & (~0ul >> 1);
          parent->stack_.dir_flag =
              &parent->left_child_locked() == cur ? StackDir::Left : StackDir::Right;

          cur_start = parent_range_start;
          cur_end = head_end;
          cur = parent;
          continue;
        }
        // Calculate the start of the region which this vmo can see but the sibling can't.
        tail_start = fbl::max(other.parent_offset_ + other.parent_limit_, parent_range_start);
      } else {
        // If the sibling can't access anything in the parent, the whole region
        // we're operating on is the 'tail' region.
        tail_start = parent_range_start;
      }

      cur->ReleaseCowParentPagesLockedHelper(start, cur_end, free_list);

      if (tail_start < parent_range_end) {
        // If the tail region is non-empty, recurse into the parent. Note that
        // we do put this vmo back on the stack, which makes it simpler to walk back
        // up the tree.
        parent->page_list_.RemovePages(tail_start, parent_range_end, free_list);

        DEBUG_ASSERT((cur_end & 1) == 0);  // cur_end is page aligned
        uint64_t scratch = cur_end >> 1;   // gcc is finicky about setting bitfields
        cur->stack_.scratch = scratch & (~0ul >> 1);
        parent->stack_.dir_flag =
            &parent->left_child_locked() == cur ? StackDir::Left : StackDir::Right;

        cur_start = tail_start;
        cur_end = parent_range_end;
        cur = parent;
        continue;
      }
    }

    if (cur == this) {
      cur = nullptr;
    } else {
      cur_start = cur_end;
      cur = cur->stack_.dir_flag == StackDir::Left ? &cur->left_child_locked()
                                                   : &cur->right_child_locked();
      cur_start -= cur->parent_offset_;
      cur_end = cur->stack_.scratch << 1;
    }
  } while (cur != nullptr);

  DEBUG_ASSERT(cur_end == root_end);
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

  Guard<fbl::Mutex> guard{&lock_};

  // make sure everything is aligned before we get started
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(s));

  list_node_t free_list;
  list_initialize(&free_list);

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
      ReleaseCowParentPagesLocked(start, end, &free_list);
    } else {
      parent_limit_ = fbl::min(parent_limit_, s);
    }
    // If the tail of a parent disappears, the children shouldn't be able to see that region
    // again, even if the parent is later reenlarged. So update the child parent limits.
    UpdateChildParentLimitsLocked(s);

    page_list_.RemovePages(start, end, &free_list);
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
    if (new_size < child.parent_offset_) {
      child.parent_limit_ = 0;
    } else {
      child.parent_limit_ = fbl::min(child.parent_limit_, new_size - child.parent_offset_);
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
                                                   T copyfunc, Guard<fbl::Mutex>* guard) {
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
    const size_t tocopy = fbl::min(PAGE_SIZE - page_offset, len);

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
                            Guard<fbl::Mutex>* guard) -> zx_status_t {
    memcpy(ptr + offset, src, len);
    return ZX_OK;
  };

  Guard<fbl::Mutex> guard{&lock_};

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
                             Guard<fbl::Mutex>* guard) -> zx_status_t {
    memcpy(dst, ptr + offset, len);
    return ZX_OK;
  };

  Guard<fbl::Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                                  void* context) {
  canary_.Assert();
  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<fbl::Mutex> guard{&lock_};

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
  auto read_routine = [ptr, &current_aspace](const char* src, size_t offset, size_t len,
                                             Guard<fbl::Mutex>* guard) -> zx_status_t {
    vaddr_t va;
    uint flags;
    zx_status_t result =
        ptr.byte_offset(offset).copy_array_to_user_capture_faults(src, len, &va, &flags);
    if (result != ZX_OK) {
      guard->CallUnlocked(
          [va, flags, &result, &current_aspace] { result = current_aspace->SoftFault(va, flags); });
      if (result == ZX_OK) {
        return ZX_ERR_SHOULD_WAIT;
      }
    }
    return result;
  };

  Guard<fbl::Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, false, read_routine, &guard);
}

zx_status_t VmObjectPaged::WriteUser(VmAspace* current_aspace, user_in_ptr<const char> ptr,
                                     uint64_t offset, size_t len) {
  canary_.Assert();

  // write routine that uses copy_from_user
  auto write_routine = [ptr, &current_aspace](char* dst, size_t offset, size_t len,
                                              Guard<fbl::Mutex>* guard) -> zx_status_t {
    vaddr_t va;
    uint flags;
    zx_status_t result =
        ptr.byte_offset(offset).copy_array_from_user_capture_faults(dst, len, &va, &flags);
    if (result != ZX_OK) {
      guard->CallUnlocked(
          [va, flags, &result, &current_aspace] { result = current_aspace->SoftFault(va, flags); });
      if (result == ZX_OK) {
        return ZX_ERR_SHOULD_WAIT;
      }
    }
    return result;
  };

  Guard<fbl::Mutex> guard{&lock_};

  return ReadWriteInternalLocked(offset, len, true, write_routine, &guard);
}

zx_status_t VmObjectPaged::TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  Guard<fbl::Mutex> src_guard{&lock_};
  uint64_t end;
  if (add_overflow(offset, len, &end) || size() < end) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (AnyPagesPinnedLocked(offset, len) || parent_ || page_source_) {
    return ZX_ERR_BAD_STATE;
  }

  // This is only used by the userpager API, which has significant restrictions on
  // what sorts of vmos are acceptable. If splice starts being used in more places,
  // then this restriction might need to be lifted.
  // TODO: Check that the region is locked once locking is implemented
  if (mapping_list_len_ || children_list_len_ ||
      AttributedPagesInRangeLocked(offset, len) != (len / PAGE_SIZE)) {
    return ZX_ERR_BAD_STATE;
  }

  *pages = page_list_.TakePages(offset, len);

  return ZX_OK;
}

zx_status_t VmObjectPaged::SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
  Guard<fbl::Mutex> guard{&lock_};
  ASSERT(page_source_);

  uint64_t end;
  if (add_overflow(offset, len, &end) || size() < end) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  list_node free_list;
  list_initialize(&free_list);

  // [new_pages_start, new_pages_start + new_pages_len) tracks the current run of
  // consecutive new pages added to this vmo.
  uint64_t new_pages_start = offset;
  uint64_t new_pages_len = 0;
  zx_status_t status = ZX_OK;
  while (!pages->IsDone()) {
    VmPageOrMarker src_page = pages->Pop();

    status = AddPageLocked(&src_page, offset);
    if (status == ZX_OK) {
      new_pages_len += PAGE_SIZE;
    } else if (src_page.IsPage()) {
      list_add_tail(&free_list, &src_page.ReleasePage()->queue_node);

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

uint32_t VmObjectPaged::GetMappingCachePolicy() const {
  Guard<fbl::Mutex> guard{&lock_};

  return cache_policy_;
}

zx_status_t VmObjectPaged::SetMappingCachePolicy(const uint32_t cache_policy) {
  // Is it a valid cache flag?
  if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<fbl::Mutex> guard{&lock_};

  // conditions for allowing the cache policy to be set:
  // 1) vmo has no pages committed currently
  // 2) vmo has no mappings
  // 3) vmo has no children
  // 4) vmo is not a child
  if (!page_list_.IsEmpty()) {
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
  while (vm_object->parent_) {
    vm_object = VmObjectPaged::AsVmObjectPaged(vm_object->parent_);
    if (!vm_object) {
      return nullptr;
    }
  }
  return vm_object->page_source_;
}

bool VmObjectPaged::IsCowClonable() const {
  Guard<fbl::Mutex> guard{&lock_};

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
    parent = p->parent_;
  }
  return true;
}

VmObjectPaged* VmObjectPaged::PagedParentOfSliceLocked(uint64_t* offset) {
  DEBUG_ASSERT(is_slice());
  VmObjectPaged* cur = this;
  uint64_t off = 0;
  while (cur->is_slice()) {
    off += cur->parent_offset_;
    DEBUG_ASSERT(cur->parent_);
    DEBUG_ASSERT(cur->parent_->is_paged());
    cur = static_cast<VmObjectPaged*>(cur->parent_.get());
  }
  *offset = off;
  return cur;
}
