// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/pinned_vm_object.h"

#include <align.h>
#include <trace.h>

#include <ktl/move.h>

#include "vm/vm.h"

#define LOCAL_TRACE 0

// static
zx_status_t PinnedVmObject::Create(fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                                   PinnedVmObject* out_pinned_vmo) {
  DEBUG_ASSERT(vmo != nullptr);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset) && IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(out_pinned_vmo != nullptr);

  zx_status_t status = vmo->CommitRangePinned(offset, size);
  if (status != ZX_OK) {
    LTRACEF("vmo->CommitRange failed: %d\n", status);
    return status;
  }

  out_pinned_vmo->vmo_ = ktl::move(vmo);
  out_pinned_vmo->offset_ = offset;
  out_pinned_vmo->size_ = size;

  return ZX_OK;
}

PinnedVmObject::PinnedVmObject() = default;

PinnedVmObject::PinnedVmObject(PinnedVmObject&&) = default;

PinnedVmObject& PinnedVmObject::operator=(PinnedVmObject&&) = default;

PinnedVmObject::~PinnedVmObject() {
  if (vmo_) {
    vmo_->Unpin(offset_, size_);
  }
}
