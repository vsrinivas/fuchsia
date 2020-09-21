// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_object_physical.h"

#include <align.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <vm/vm.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

VmObjectPhysical::VmObjectPhysical(fbl::RefPtr<vm_lock_t> lock, paddr_t base, uint64_t size,
                                   bool is_slice)
    : VmObject(ktl::move(lock)), size_(size), base_(base), is_slice_(is_slice) {
  LTRACEF("%p, size %#" PRIx64 "\n", this, size_);

  DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));

  AddToGlobalList();
}

VmObjectPhysical::~VmObjectPhysical() {
  canary_.Assert();
  LTRACEF("%p\n", this);

  {
    Guard<Mutex> guard{&lock_};
    if (parent_) {
      parent_->RemoveChild(this, guard.take());
    }
  }

  RemoveFromGlobalList();
}

zx_status_t VmObjectPhysical::Create(paddr_t base, uint64_t size, fbl::RefPtr<VmObject>* obj) {
  if (!IS_PAGE_ALIGNED(base) || !IS_PAGE_ALIGNED(size) || size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // check that base + size is a valid range
  paddr_t safe_base;
  if (add_overflow(base, size - 1, &safe_base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto lock = fbl::AdoptRef<vm_lock_t>(new (&ac) vm_lock_t);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto vmo = fbl::AdoptRef<VmObject>(
      new (&ac) VmObjectPhysical(ktl::move(lock), base, size, /*is_slice=*/false));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Physical VMOs should default to uncached access.
  vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_UNCACHED);

  *obj = ktl::move(vmo);

  return ZX_OK;
}

zx_status_t VmObjectPhysical::CreateChildSlice(uint64_t offset, uint64_t size, bool copy_name,
                                               fbl::RefPtr<VmObject>* child_vmo) {
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

  // Forbid creating children of resizable VMOs. This restriction may be lifted in the future.
  if (is_resizable()) {
    return ZX_ERR_NOT_SUPPORTED;
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

  // To mimic a slice we can just create a physical vmo with the correct region. This works since
  // nothing is resizable and the slice must be wholly contained.
  fbl::AllocChecker ac;
  auto vmo = fbl::AdoptRef<VmObjectPhysical>(
      new (&ac) VmObjectPhysical(lock_ptr_, base_ + offset, size, /*is_slice=*/true));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  bool notify_one_child;
  {
    Guard<Mutex> guard{&lock_};

    // Inherit the current cache policy
    vmo->mapping_cache_flags_ = mapping_cache_flags_;
    // Initialize parent
    vmo->parent_ = fbl::RefPtr(this);
    // Initialize parent user id.
    vmo->parent_user_id_ = user_id_locked();

    // add the new vmo as a child.
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

void VmObjectPhysical::Dump(uint depth, bool verbose) {
  canary_.Assert();

  Guard<Mutex> guard{&lock_};
  for (uint i = 0; i < depth; ++i) {
    printf("  ");
  }
  printf("object %p base %#" PRIxPTR " size %#" PRIx64 " ref %d\n", this, base_, size_,
         ref_count_debug());
}

// get the physical address of a page at offset
zx_status_t VmObjectPhysical::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                            PageRequest* page_request, vm_page_t** _page,
                                            paddr_t* _pa) {
  canary_.Assert();

  if (_page) {
    *_page = nullptr;
  }

  if (offset >= size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t pa = base_ + ROUNDDOWN(offset, PAGE_SIZE);
  if (pa > UINTPTR_MAX) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *_pa = (paddr_t)pa;

  return ZX_OK;
}

zx_status_t VmObjectPhysical::Lookup(uint64_t offset, uint64_t len, vmo_lookup_fn_t lookup_fn,
                                     void* context) {
  canary_.Assert();

  if (unlikely(len == 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t cur_offset = ROUNDDOWN(offset, PAGE_SIZE);
  uint64_t end = offset + len;
  uint64_t end_page_offset = ROUNDUP(end, PAGE_SIZE);

  for (size_t idx = 0; cur_offset < end_page_offset; cur_offset += PAGE_SIZE, ++idx) {
    zx_status_t status = lookup_fn(context, cur_offset, idx, base_ + cur_offset);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t VmObjectPhysical::CommitRangePinned(uint64_t offset, uint64_t len) {
  canary_.Assert();

  if (unlikely(len == 0 || !IS_PAGE_ALIGNED(offset))) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};
  if (unlikely(!InRange(offset, len, size_))) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // Physical VMOs are always committed and so are always pinned.
  return ZX_OK;
}

uint32_t VmObjectPhysical::GetMappingCachePolicy() const {
  Guard<Mutex> guard{&lock_};

  return mapping_cache_flags_;
}

zx_status_t VmObjectPhysical::SetMappingCachePolicy(const uint32_t cache_policy) {
  // Is it a valid cache flag?
  if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  // If the cache policy is already configured on this VMO and matches
  // the requested policy then this is a no-op. This is a common practice
  // in the serialio and magma drivers, but may change.
  // TODO: revisit this when we shake out more of the future DDK protocol.
  if (cache_policy == mapping_cache_flags_) {
    return ZX_OK;
  }

  // If this VMO is mapped already it is not safe to allow its caching policy to change
  if (mapping_list_len_ != 0 || children_list_len_ != 0 || parent_) {
    LTRACEF(
        "Warning: trying to change cache policy while this vmo has mappings, children or a "
        "parent!\n");
    return ZX_ERR_BAD_STATE;
  }

  mapping_cache_flags_ = cache_policy;
  return ZX_OK;
}
