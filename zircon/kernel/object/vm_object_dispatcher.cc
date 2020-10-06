// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/vm_object_dispatcher.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <trace.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_vmo_create_count, "dispatcher.vmo.create")
KCOUNTER(dispatcher_vmo_destroy_count, "dispatcher.vmo.destroy")
// TODO(fxbug.dev/60795): Remove ZX_VMO_OP_CACHE_INVALIDATE
KCOUNTER(dispatcher_vmo_op_cache_invalidate, "dispatcher.vmo.op_cache_invalidate")

zx_status_t VmObjectDispatcher::parse_create_syscall_flags(uint32_t flags, uint32_t* out_flags) {
  uint32_t res = 0;
  if (flags & ZX_VMO_RESIZABLE) {
    res |= VmObjectPaged::kResizable;
    flags &= ~ZX_VMO_RESIZABLE;
  }

  if (flags) {
    return ZX_ERR_INVALID_ARGS;
  }

  *out_flags = res;

  return ZX_OK;
}

zx_status_t VmObjectDispatcher::Create(fbl::RefPtr<VmObject> vmo, zx_koid_t pager_koid,
                                       KernelHandle<VmObjectDispatcher>* handle,
                                       zx_rights_t* rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) VmObjectDispatcher(ktl::move(vmo), pager_koid)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  new_handle.dispatcher()->vmo()->set_user_id(new_handle.dispatcher()->get_koid());
  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

VmObjectDispatcher::VmObjectDispatcher(fbl::RefPtr<VmObject> vmo, zx_koid_t pager_koid)
    : SoloDispatcher(ZX_VMO_ZERO_CHILDREN), vmo_(vmo), pager_koid_(pager_koid) {
  kcounter_add(dispatcher_vmo_create_count, 1);
  vmo_->SetChildObserver(this);
}

VmObjectDispatcher::~VmObjectDispatcher() {
  kcounter_add(dispatcher_vmo_destroy_count, 1);
  // Intentionally leave vmo_->user_id() set to our koid even though we're
  // dying and the koid will no longer map to a Dispatcher. koids are never
  // recycled, and it could be a useful breadcrumb.
}

void VmObjectDispatcher::OnZeroChild() { UpdateState(0, ZX_VMO_ZERO_CHILDREN); }

void VmObjectDispatcher::OnOneChild() { UpdateState(ZX_VMO_ZERO_CHILDREN, 0); }

void VmObjectDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  canary_.Assert();
  vmo_->get_name(out_name, ZX_MAX_NAME_LEN);
}

zx_status_t VmObjectDispatcher::set_name(const char* name, size_t len) {
  canary_.Assert();
  return vmo_->set_name(name, len);
}

void VmObjectDispatcher::on_zero_handles() {
  // Clear when handle count reaches zero rather in the destructor because we're retaining a
  // VmObject that might call back into |this| via VmObjectChildObserver when it's destroyed.
  vmo_->SetChildObserver(nullptr);
}

zx_status_t VmObjectDispatcher::Read(VmAspace* current_aspace, user_out_ptr<char> user_data,
                                     size_t length, uint64_t offset) {
  canary_.Assert();

  return vmo_->ReadUser(current_aspace, user_data, offset, length);
}

zx_status_t VmObjectDispatcher::ReadVector(VmAspace* current_aspace, user_out_iovec_t user_data,
                                           size_t length, uint64_t offset) {
  canary_.Assert();

  return vmo_->ReadUserVector(current_aspace, user_data, offset, length);
}

zx_status_t VmObjectDispatcher::WriteVector(VmAspace* current_aspace, user_in_iovec_t user_data,
                                            size_t length, uint64_t offset) {
  canary_.Assert();

  return vmo_->WriteUserVector(current_aspace, user_data, offset, length);
}

zx_status_t VmObjectDispatcher::Write(VmAspace* current_aspace, user_in_ptr<const char> user_data,
                                      size_t length, uint64_t offset) {
  canary_.Assert();

  return vmo_->WriteUser(current_aspace, user_data, offset, length);
}

zx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
  canary_.Assert();

  return vmo_->Resize(size);
}

zx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
  canary_.Assert();

  *size = vmo_->size();

  return ZX_OK;
}

zx_info_vmo_t VmoToInfoEntry(const VmObject* vmo, bool is_handle, zx_rights_t handle_rights) {
  zx_info_vmo_t entry = {};
  entry.koid = vmo->user_id();
  vmo->get_name(entry.name, sizeof(entry.name));
  entry.size_bytes = vmo->size();
  entry.parent_koid = vmo->parent_user_id();
  entry.num_children = vmo->num_user_children();
  entry.num_mappings = vmo->num_mappings();
  entry.share_count = vmo->share_count();
  entry.flags = (vmo->is_paged() ? ZX_INFO_VMO_TYPE_PAGED : ZX_INFO_VMO_TYPE_PHYSICAL) |
                (vmo->is_resizable() ? ZX_INFO_VMO_RESIZABLE : 0) |
                (vmo->is_pager_backed() ? ZX_INFO_VMO_PAGER_BACKED : 0) |
                (vmo->is_contiguous() ? ZX_INFO_VMO_CONTIGUOUS : 0);
  entry.committed_bytes = vmo->AttributedPages() * PAGE_SIZE;
  entry.cache_policy = vmo->GetMappingCachePolicy();
  if (is_handle) {
    entry.flags |= ZX_INFO_VMO_VIA_HANDLE;
    entry.handle_rights = handle_rights;
  } else {
    entry.flags |= ZX_INFO_VMO_VIA_MAPPING;
  }
  if (vmo->child_type() == VmObject::ChildType::kCowClone) {
    entry.flags |= ZX_INFO_VMO_IS_COW_CLONE;
  }
  entry.metadata_bytes = vmo->HeapAllocationBytes();
  // Only events that change committed pages are evictions at the moment.
  entry.committed_change_events = vmo->EvictedPagedCount();
  return entry;
}

zx_info_vmo_t VmObjectDispatcher::GetVmoInfo(void) { return VmoToInfoEntry(vmo().get(), true, 0); }

zx_status_t VmObjectDispatcher::SetContentSize(uint64_t content_size) {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};
  content_size_ = content_size;
  return ZX_OK;
}

uint64_t VmObjectDispatcher::GetContentSize() const {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};
  return content_size_;
}

uint64_t VmObjectDispatcher::ExpandContentIfNeeded(uint64_t requested_content_size,
                                                   uint64_t zero_until_offset) {
  canary_.Assert();

  Guard<Mutex> guard{get_lock()};
  if (requested_content_size <= content_size_) {
    return content_size_;
  }

  uint64_t previous_content_size = content_size_;

  do {
    uint64_t required_vmo_size = ROUNDUP(requested_content_size, PAGE_SIZE);
    uint64_t current_vmo_size = vmo_->size();
    if (required_vmo_size <= current_vmo_size) {
      content_size_ = requested_content_size;
      break;
    }

    zx_status_t status = vmo_->Resize(required_vmo_size);
    if (status != ZX_OK) {
      content_size_ = current_vmo_size;
      break;
    }

    content_size_ = requested_content_size;
  } while (false);

  zero_until_offset = ktl::min(content_size_, zero_until_offset);
  if (zero_until_offset > previous_content_size) {
    vmo_->ZeroRange(previous_content_size, zero_until_offset - previous_content_size);
  }

  return content_size_;
}

zx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_inout_ptr<char> buffer, size_t buffer_size,
                                        zx_rights_t rights) {
  canary_.Assert();

  LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64 " buffer %p buffer_size %zu rights %#x\n", op,
          offset, size, buffer.get(), buffer_size, rights);

  switch (op) {
    case ZX_VMO_OP_COMMIT: {
      if ((rights & ZX_RIGHT_WRITE) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      // TODO: handle partial commits
      auto status = vmo_->CommitRange(offset, size);
      return status;
    }
    case ZX_VMO_OP_DECOMMIT: {
      if ((rights & ZX_RIGHT_WRITE) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      // TODO: handle partial decommits
      auto status = vmo_->DecommitRange(offset, size);
      return status;
    }
    case ZX_VMO_OP_LOCK:
    case ZX_VMO_OP_UNLOCK:
      // TODO: handle or remove
      return ZX_ERR_NOT_SUPPORTED;

    case ZX_VMO_OP_CACHE_SYNC:
      if ((rights & ZX_RIGHT_READ) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      return vmo_->SyncCache(offset, size);
    case ZX_VMO_OP_CACHE_INVALIDATE:
      kcounter_add(dispatcher_vmo_op_cache_invalidate, 1);
      // A straight invalidate op requires the write right since
      // it may drop dirty cache lines, thus modifying the contents
      // of the VMO.
      if ((rights & ZX_RIGHT_WRITE) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      return vmo_->InvalidateCache(offset, size);
    case ZX_VMO_OP_CACHE_CLEAN:
      if ((rights & ZX_RIGHT_READ) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      return vmo_->CleanCache(offset, size);
    case ZX_VMO_OP_CACHE_CLEAN_INVALIDATE:
      if ((rights & ZX_RIGHT_READ) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      return vmo_->CleanInvalidateCache(offset, size);
    case ZX_VMO_OP_ZERO:
      if ((rights & ZX_RIGHT_WRITE) == 0) {
        return ZX_ERR_ACCESS_DENIED;
      }
      return vmo_->ZeroRange(offset, size);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t VmObjectDispatcher::SetMappingCachePolicy(uint32_t cache_policy) {
  return vmo_->SetMappingCachePolicy(cache_policy);
}

zx_status_t VmObjectDispatcher::CreateChild(uint32_t options, uint64_t offset, uint64_t size,
                                            bool copy_name, fbl::RefPtr<VmObject>* child_vmo) {
  canary_.Assert();

  LTRACEF("options 0x%x offset %#" PRIx64 " size %#" PRIx64 "\n", options, offset, size);

  if (options & ZX_VMO_CHILD_SLICE) {
    // No other flags are valid for slices.
    options &= ~ZX_VMO_CHILD_SLICE;
    if (options) {
      return ZX_ERR_INVALID_ARGS;
    }
    return vmo_->CreateChildSlice(offset, size, copy_name, child_vmo);
  }

  // Check for mutually-exclusive child type flags.
  CloneType type;
  if (options & ZX_VMO_CHILD_SNAPSHOT) {
    options &= ~ZX_VMO_CHILD_SNAPSHOT;
    type = CloneType::Snapshot;
  } else if (options & ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE) {
    options &= ~ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE;
    if (vmo_->is_pager_backed()) {
      type = CloneType::PrivatePagerCopy;
    } else {
      type = CloneType::Snapshot;
    }
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  Resizability resizable = Resizability::NonResizable;
  if (options & ZX_VMO_CHILD_RESIZABLE) {
    resizable = Resizability::Resizable;
    options &= ~ZX_VMO_CHILD_RESIZABLE;
  }

  if (options)
    return ZX_ERR_INVALID_ARGS;

  return vmo_->CreateClone(resizable, type, offset, size, copy_name, child_vmo);
}
