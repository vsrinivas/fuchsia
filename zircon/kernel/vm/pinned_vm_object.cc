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

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// static
zx_status_t PinnedVmObject::Create(fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                                   bool write, PinnedVmObject* out_pinned_vmo) {
  DEBUG_ASSERT(vmo != nullptr);
  DEBUG_ASSERT(IS_PAGE_ALIGNED(offset) && IS_PAGE_ALIGNED(size));
  DEBUG_ASSERT(out_pinned_vmo != nullptr);

  PinnedVmObject pinned_vmo;

  zx_status_t status = vmo->CommitRangePinned(offset, size, write);
  if (status != ZX_OK) {
    LTRACEF("vmo->CommitRange failed: %d\n", status);
    return status;
  }

  pinned_vmo.vmo_ = ktl::move(vmo);
  pinned_vmo.offset_ = offset;
  pinned_vmo.size_ = size;

  *out_pinned_vmo = ktl::move(pinned_vmo);

  return ZX_OK;
}

void PinnedVmObject::reset() {
  if (vmo_) {
    vmo_->Unpin(offset_, size_);
    vmo_.reset();
  }
}

PinnedVmObject::PinnedVmObject() = default;

PinnedVmObject::PinnedVmObject(PinnedVmObject&&) noexcept = default;

PinnedVmObject& PinnedVmObject::operator=(PinnedVmObject&& pinned) noexcept {
  reset();
  vmo_ = ktl::move(pinned.vmo_);
  offset_ = pinned.offset_;
  size_ = pinned.size_;
  return *this;
}

PinnedVmObject::~PinnedVmObject() { reset(); }
