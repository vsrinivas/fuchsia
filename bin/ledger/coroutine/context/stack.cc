// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/coroutine/context/stack.h"

#include <stdlib.h>

#include "lib/fxl/logging.h"
#include "mx/vmar.h"

namespace context {

constexpr size_t kStackGuardSize = PAGE_SIZE;

namespace {
size_t ToFullPages(size_t value) {
  return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
}

void AllocateStack(const mx::vmo& vmo,
                   size_t vmo_offset,
                   size_t stack_size,
                   mx::vmar* vmar,
                   uintptr_t* addr) {
  uintptr_t allocate_address;
  mx_status_t status = mx::vmar::root_self().allocate(
      0, stack_size + 2 * kStackGuardSize,
      MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
          MX_VM_FLAG_CAN_MAP_SPECIFIC,
      vmar, &allocate_address);
  FXL_DCHECK(status == MX_OK);

  status = vmar->map(
      kStackGuardSize, vmo, vmo_offset, stack_size,
      MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC, addr);
  FXL_DCHECK(status == MX_OK);
}
}  // namespace

Stack::Stack(size_t stack_size) : stack_size_(ToFullPages(stack_size)) {
  FXL_DCHECK(stack_size_);

  mx_status_t status = mx::vmo::create(2 * stack_size_, 0, &vmo_);
  FXL_DCHECK(status == MX_OK);

  AllocateStack(vmo_, 0, stack_size_, &safe_stack_mapping_, &safe_stack_);
  AllocateStack(vmo_, stack_size_, stack_size_, &unsafe_stack_mapping_,
                &unsafe_stack_);

  FXL_DCHECK(safe_stack_);
  FXL_DCHECK(unsafe_stack_);
}

Stack::~Stack() {
  safe_stack_mapping_.destroy();
  unsafe_stack_mapping_.destroy();
}

void Stack::Release() {
  mx_status_t status =
      vmo_.op_range(MX_VMO_OP_DECOMMIT, 0, 2 * stack_size_, nullptr, 0);
  FXL_DCHECK(status == MX_OK);
}

}  // namespace context
