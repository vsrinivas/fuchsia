// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_object.h"

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object_paged.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

VmObject::GlobalList VmObject::all_vmos_ = {};

fbl::DoublyLinkedList<VmObject::Cursor*> VmObject::all_vmos_cursors_ = {};

VmObject::VmObject(fbl::RefPtr<VmHierarchyState> hierarchy_state_ptr)
    : VmHierarchyBase(ktl::move(hierarchy_state_ptr)) {
  LTRACEF("%p\n", this);
}

VmObject::~VmObject() {
  canary_.Assert();
  LTRACEF("%p\n", this);

  DEBUG_ASSERT(!InGlobalList());

  DEBUG_ASSERT(mapping_list_.is_empty());
  DEBUG_ASSERT(children_list_.is_empty());
}

void VmObject::AddToGlobalList() {
  Guard<CriticalMutex> guard{AllVmosLock::Get()};
  all_vmos_.push_back(this);
}

void VmObject::RemoveFromGlobalList() {
  Guard<CriticalMutex> guard{AllVmosLock::Get()};
  DEBUG_ASSERT(InGlobalList());
  Cursor::AdvanceCursors(all_vmos_cursors_, this);
  all_vmos_.erase(*this);
}

void VmObject::get_name(char* out_name, size_t len) const {
  canary_.Assert();
  name_.get(len, out_name);
}

zx_status_t VmObject::set_name(const char* name, size_t len) {
  canary_.Assert();
  return name_.set(name, len);
}

void VmObject::set_user_id(uint64_t user_id) {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(user_id_ == 0);
  user_id_ = user_id;
}

uint64_t VmObject::user_id() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  return user_id_;
}

uint64_t VmObject::user_id_locked() const { return user_id_; }

void VmObject::AddMappingLocked(VmMapping* r) {
  canary_.Assert();
  mapping_list_.push_front(r);
  mapping_list_len_++;
}

void VmObject::RemoveMappingLocked(VmMapping* r) {
  canary_.Assert();
  mapping_list_.erase(*r);
  DEBUG_ASSERT(mapping_list_len_ > 0);
  mapping_list_len_--;
}

uint32_t VmObject::num_mappings() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  return mapping_list_len_;
}

bool VmObject::IsMappedByUser() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  return ktl::any_of(mapping_list_.cbegin(), mapping_list_.cend(),
                     [](const VmMapping& m) -> bool { return m.aspace()->is_user(); });
}

uint32_t VmObject::share_count() const {
  canary_.Assert();

  Guard<CriticalMutex> guard{&lock_};
  if (mapping_list_len_ < 2) {
    return 1;
  }

  // Find the number of unique VmAspaces that we're mapped into.
  // Use this buffer to hold VmAspace pointers.
  static constexpr int kAspaceBuckets = 64;
  uintptr_t aspaces[kAspaceBuckets];
  unsigned int num_mappings = 0;  // Number of mappings we've visited
  unsigned int num_aspaces = 0;   // Unique aspaces we've seen
  for (const auto& m : mapping_list_) {
    uintptr_t as = reinterpret_cast<uintptr_t>(m.aspace().get());
    // Simple O(n^2) should be fine.
    for (unsigned int i = 0; i < num_aspaces; i++) {
      if (aspaces[i] == as) {
        goto found;
      }
    }
    if (num_aspaces < kAspaceBuckets) {
      aspaces[num_aspaces++] = as;
    } else {
      // Maxed out the buffer. Estimate the remaining number of aspaces.
      num_aspaces +=
          // The number of mappings we haven't visited yet
          (mapping_list_len_ - num_mappings)
          // Scaled down by the ratio of unique aspaces we've seen so far.
          * num_aspaces / num_mappings;
      break;
    }
  found:
    num_mappings++;
  }
  DEBUG_ASSERT_MSG(num_aspaces <= mapping_list_len_,
                   "num_aspaces %u should be <= mapping_list_len_ %" PRIu32, num_aspaces,
                   mapping_list_len_);

  // TODO: Cache this value as long as the set of mappings doesn't change.
  // Or calculate it when adding/removing a new mapping under an aspace
  // not in the list.
  return num_aspaces;
}

zx_status_t VmObject::ReadUserVector(VmAspace* current_aspace, user_out_iovec_t vec,
                                     uint64_t offset, size_t len, size_t* out_actual) {
  if (len == 0u) {
    return ZX_OK;
  }
  if (len > UINT64_MAX - offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return vec.ForEach([&](user_out_ptr<char> ptr, size_t capacity) {
    if (capacity > len) {
      capacity = len;
    }

    size_t chunk_actual = 0;
    zx_status_t status = ReadUser(current_aspace, ptr, offset, capacity, &chunk_actual);

    // Always add |chunk_actual| since some bytes may have been transferred, even on error
    if (out_actual != nullptr) {
      *out_actual += chunk_actual;
    }
    if (status != ZX_OK) {
      return status;
    }

    DEBUG_ASSERT(chunk_actual == capacity);

    offset += chunk_actual;
    len -= chunk_actual;
    return len > 0 ? ZX_ERR_NEXT : ZX_ERR_STOP;
  });
}

zx_status_t VmObject::WriteUserVector(VmAspace* current_aspace, user_in_iovec_t vec,
                                      uint64_t offset, size_t len, size_t* out_actual,
                                      const OnWriteBytesTransferredCallback& on_bytes_transferred) {
  if (len == 0u) {
    return ZX_OK;
  }
  if (len > UINT64_MAX - offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return vec.ForEach([&](user_in_ptr<const char> ptr, size_t capacity) {
    if (capacity > len) {
      capacity = len;
    }

    size_t chunk_actual = 0;
    zx_status_t status =
        WriteUser(current_aspace, ptr, offset, capacity, &chunk_actual, on_bytes_transferred);

    // Always add |chunk_actual| since some bytes may have been transferred, even on error
    if (out_actual != nullptr) {
      *out_actual += chunk_actual;
    }
    if (status != ZX_OK) {
      return status;
    }

    DEBUG_ASSERT(chunk_actual == capacity);

    offset += chunk_actual;
    len -= chunk_actual;
    return len > 0 ? ZX_ERR_NEXT : ZX_ERR_STOP;
  });
}

void VmObject::SetChildObserver(VmObjectChildObserver* child_observer) {
  Guard<Mutex> guard{&child_observer_lock_};
  child_observer_ = child_observer;
}

bool VmObject::AddChildLocked(VmObject* child) {
  canary_.Assert();
  children_list_.push_front(child);
  children_list_len_++;

  return children_list_len_ == 1;
}

void VmObject::NotifyOneChild() {
  canary_.Assert();

  // Make sure we're not holding the shared lock while notifying the observer in case it calls
  // back into this object.
  DEBUG_ASSERT(!lock_.lock().IsHeld());

  Guard<Mutex> observer_guard{&child_observer_lock_};

  // Signal the dispatcher that there are child VMOS
  if (child_observer_ != nullptr) {
    child_observer_->OnOneChild();
  }
}

void VmObject::ReplaceChildLocked(VmObject* old, VmObject* new_child) {
  canary_.Assert();
  children_list_.replace(*old, new_child);
}

void VmObject::DropChildLocked(VmObject* c) {
  canary_.Assert();
  DEBUG_ASSERT(children_list_len_ > 0);
  children_list_.erase(*c);
  --children_list_len_;
}

void VmObject::RemoveChild(VmObject* o, Guard<CriticalMutex>&& adopt) {
  canary_.Assert();
  DEBUG_ASSERT(adopt.wraps_lock(lock_ref().lock()));

  // The observer may call back into this object so we must release the shared lock to prevent any
  // self-deadlock. We explicitly release the lock prior to acquiring the child_observer_lock as
  // otherwise we have lock ordering issue, since we already allow the shared lock to be acquired
  // whilst holding the child_observer_lock.
  {
    Guard<CriticalMutex> guard{AdoptLock, ktl::move(adopt)};
    DropChildLocked(o);

    if (children_list_len_ != 0) {
      return;
    }
  }

  {
    Guard<Mutex> observer_guard{&child_observer_lock_};

    // Signal the dispatcher that there are no more child VMOS
    if (child_observer_ != nullptr) {
      child_observer_->OnZeroChild();
    }
  }
}

uint32_t VmObject::num_children() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{&lock_};
  return children_list_len_;
}

// static
void VmObject::CacheOpPhys(paddr_t pa, uint64_t len, CacheOpType type,
                           ArchVmICacheConsistencyManager& cm) {
  DEBUG_ASSERT(is_physmap_phys_addr(pa));
  DEBUG_ASSERT(len > 0);

  const vaddr_t va = reinterpret_cast<vaddr_t>(paddr_to_physmap(pa));

  switch (type) {
    case CacheOpType::Invalidate:
      arch_invalidate_cache_range(va, len);
      break;
    case CacheOpType::Clean:
      arch_clean_cache_range(va, len);
      break;
    case CacheOpType::CleanInvalidate:
      arch_clean_invalidate_cache_range(va, len);
      break;
    case CacheOpType::Sync:
      cm.SyncAddr(va, len);
      break;
  }
}

// round up the size to the next page size boundary and make sure we dont wrap
zx_status_t VmObject::RoundSize(uint64_t size, uint64_t* out_size) {
  *out_size = ROUNDUP_PAGE_SIZE(size);
  if (*out_size < size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // there's a max size to keep indexes within range
  if (*out_size > MAX_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

zx_status_t VmObject::GetPageBlocking(uint64_t offset, uint pf_flags, list_node* alloc_list,
                                      vm_page_t** page, paddr_t* pa) {
  zx_status_t status = ZX_OK;
  // TOOD(fxb/94078): Enforce no locks held as this might wait whilst holding a lock.
  __UNINITIALIZED LazyPageRequest page_request;
  do {
    status = GetPage(offset, pf_flags, alloc_list, &page_request, page, pa);
    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_status_t st = page_request->Wait();
      if (st != ZX_OK) {
        return st;
      }
    }
  } while (status == ZX_ERR_SHOULD_WAIT);

  return status;
}

VmHierarchyBase::VmHierarchyBase(fbl::RefPtr<VmHierarchyState> state)
    : lock_(state->lock_ref()), hierarchy_state_ptr_(ktl::move(state)) {}

void VmHierarchyState::DoDeferredDelete(fbl::RefPtr<VmHierarchyBase> vmo) {
  Guard<CriticalMutex> guard{&lock_};
  // If a parent has multiple children then it's possible for a given object to already be
  // queued for deletion.
  if (!vmo->deferred_delete_state_.InContainer()) {
    delete_list_.push_front(ktl::move(vmo));
  } else {
    // We know a refptr is being held by the container (which we are holding the lock to), so can
    // safely drop the vmo ref.
    vmo.reset();
  }
  if (!running_delete_) {
    running_delete_ = true;
    while (!delete_list_.is_empty()) {
      guard.CallUnlocked([ptr = delete_list_.pop_front()]() mutable { ptr.reset(); });
    }
    running_delete_ = false;
  }
}

static int cmd_vm_object(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s dump <address>\n", argv[0].str);
    printf("%s dump_pages <address>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

    o->Dump(0, false);
  } else if (!strcmp(argv[1].str, "dump_pages")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

    o->Dump(0, true);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("vm_object", "vm object debug commands", &cmd_vm_object)
STATIC_COMMAND_END(vm_object)
