// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>
#include <fbl/auto_lock.h>

#include "baseclock.h"
#include "clocktree.h"
#include "types.h"

namespace clk {

namespace {

constexpr bool IsError(const zx_status_t st) {
  // A clock may or may not choose to implement any of the core clock operations.
  // If an operation is not implemented the method must return ZX_ERR_NOT_SUPPORTED
  // which is not considered an error.
  return st != ZX_OK && st != ZX_ERR_NOT_SUPPORTED;
}

}  // namespace

zx_status_t Tree::Enable(const uint32_t id) {
  fbl::AutoLock lock(&topology_mutex_);
  return EnableLocked(id);
}
zx_status_t Tree::EnableLocked(const uint32_t id) {
  if (id == kClkNoParent) {
    return ZX_OK;
  }  // At the root.
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];
  const uint32_t parent_id = self->ParentId();
  const uint32_t self_enable_count = self->EnableCount();

  if (self_enable_count == 0) {
    zx_status_t parent_enable_st = EnableLocked(parent_id);
    if (IsError(parent_enable_st)) {
      return parent_enable_st;
    }

    zx_status_t st = self->Enable();

    if (IsError(st)) {
      DisableLocked(parent_id);
      return st;
    }
  }

  self->SetEnableCount(self_enable_count + 1);

  return ZX_OK;
}

zx_status_t Tree::Disable(const uint32_t id) {
  fbl::AutoLock lock(&topology_mutex_);
  return DisableLocked(id);
}
zx_status_t Tree::DisableLocked(const uint32_t id) {
  if (id == kClkNoParent) {
    return ZX_OK;
  }  // At the root.
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];
  const uint32_t parent_id = self->ParentId();

  if (self->EnableCount() == 0) {
    return ZX_ERR_BAD_STATE;
  }

  // Decremet the refs.
  self->SetEnableCount(self->EnableCount() - 1);

  if (self->EnableCount() > 0) {
    return ZX_OK;
  }

  // At this point we're about to disable the clock so we should definitely
  // have 0 refs.
  ZX_ASSERT(self->EnableCount() == 0);

  // Disable this clock and then disable its parent. Don't try to unwind if
  // disable fails.
  zx_status_t self_st = self->Disable();
  zx_status_t parent_st = DisableLocked(parent_id);

  // If this clock fails to disable and a clock somewhere in the parent chain
  // fails to disable, return the error caused by the clock closest to the
  // caller (i.e. this clock).
  if (IsError(self_st)) {
    return self_st;
  }
  if (IsError(parent_st)) {
    return parent_st;
  }

  return ZX_OK;
}

zx_status_t Tree::IsEnabled(const uint32_t id, bool* out) {
  fbl::AutoLock lock(&topology_mutex_);
  return IsEnabledLocked(id, out);
}
zx_status_t Tree::IsEnabledLocked(const uint32_t id, bool* out) {
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];

  return self->IsEnabled(out);
}

zx_status_t Tree::SetRate(const uint32_t id, const Hertz rate) {
  fbl::AutoLock lock(&topology_mutex_);
  return SetRateLocked(id, rate);
}
zx_status_t Tree::SetRateLocked(const uint32_t id, const Hertz rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tree::QuerySupportedRate(const uint32_t id, const Hertz max) {
  fbl::AutoLock lock(&topology_mutex_);
  return QuerySupportedRateLocked(id, max);
}
zx_status_t Tree::QuerySupportedRateLocked(const uint32_t id, const Hertz max) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tree::GetRate(const uint32_t id, Hertz* out) {
  fbl::AutoLock lock(&topology_mutex_);
  return GetRateLocked(id, out);
}
zx_status_t Tree::GetRateLocked(const uint32_t id, Hertz* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Tree::SetInput(const uint32_t id, const uint32_t input_index) {
  fbl::AutoLock lock(&topology_mutex_);
  return SetInputLocked(id, input_index);
}
zx_status_t Tree::SetInputLocked(const uint32_t id, const uint32_t input_index) {
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];

  uint32_t old_parent_id = self->ParentId();
  uint32_t new_parent_id;
  zx_status_t st = self->GetInputId(input_index, &new_parent_id);
  if (st != ZX_OK) {
    return st;
  }

  if (!InRange(new_parent_id)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (old_parent_id == new_parent_id) {
    // Input is already set correctly, no work to do.
    return ZX_OK;
  }

  const bool should_migrate_refs = self->EnableCount() > 0;

  // (1) if `self` is enabled then it should add a ref to its new parent.
  if (should_migrate_refs) {
    st = EnableLocked(new_parent_id);
    if (st != ZX_OK) {
      return st;
    }
  }

  // (2) Perform the reparent operation.
  st = self->SetInput(input_index);
  if (st != ZX_OK) {
    if (should_migrate_refs) {
      DisableLocked(new_parent_id);
    }
    return st;
  }

  // (3) If `self` is enabled then it should remember to drop a ref to its old
  //     parent after the reparent operation completes.
  if (should_migrate_refs) {
    DisableLocked(old_parent_id);
  }

  return ZX_OK;
}

zx_status_t Tree::GetNumInputs(const uint32_t id, uint32_t* out) {
  fbl::AutoLock lock(&topology_mutex_);
  return GetNumInputsLocked(id, out);
}
zx_status_t Tree::GetNumInputsLocked(const uint32_t id, uint32_t* out) {
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];

  return self->GetNumInputs(out);
}
zx_status_t Tree::GetInput(const uint32_t id, uint32_t* out) {
  fbl::AutoLock lock(&topology_mutex_);
  return GetInputLocked(id, out);
}
zx_status_t Tree::GetInputLocked(const uint32_t id, uint32_t* out) {
  if (!InRange(id)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  BaseClock* self = clocks_[id];

  return self->GetInput(out);
}

// Helper functions
bool Tree::InRange(const uint32_t index) const { return index < count_; }

}  // namespace clk
