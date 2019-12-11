// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/coroutine/context/stack.h"

#include <lib/zx/vmar.h>
#include <stdlib.h>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"

namespace context {

namespace {
size_t ToFullPages(size_t value) { return (value + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)); }

// ASAN doesn't instrument vmo mappings. Use traditional malloc/free when
// running with ASAN.
#if __has_feature(address_sanitizer)
// ASAN doesn't support safe stack.
static_assert(!__has_feature(safe_stack), "Add support for safe stack under ASAN");

void AllocateASAN(size_t stack_size, uintptr_t* stack) {
  *stack = reinterpret_cast<uintptr_t>(malloc(stack_size));
  LEDGER_DCHECK(*stack);
}

void ReleaseASAN(uintptr_t stack) { free(reinterpret_cast<void*>(stack)); }

#else  // __has_feature(address_sanitizer)

constexpr size_t kStackGuardSize = PAGE_SIZE;

#if __has_feature(safe_stack)
constexpr size_t kVmoSizeMultiplier = 2u;
#else
constexpr size_t kVmoSizeMultiplier = 1u;
#endif

size_t VmoSize(size_t stack_size) {
  return (kVmoSizeMultiplier * stack_size) + Stack::shadow_call_stack_size();
}

void AllocateStack(const zx::vmo& vmo, size_t vmo_offset, size_t stack_size, zx::vmar* vmar,
                   uintptr_t* addr) {
  uintptr_t allocate_address;
  zx_status_t status = zx::vmar::root_self()->allocate(
      0, stack_size + 2 * kStackGuardSize,
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, vmar, &allocate_address);
  LEDGER_DCHECK(status == ZX_OK);

  status = vmar->map(kStackGuardSize, vmo, vmo_offset, stack_size,
                     ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, addr);
  LEDGER_DCHECK(status == ZX_OK);
}

#endif  // __has_feature(address_sanitizer)
}  // namespace

#if __has_feature(address_sanitizer)

Stack::Stack(size_t stack_size) : stack_size_(ToFullPages(stack_size)) {
  LEDGER_DCHECK(stack_size_);

  AllocateASAN(stack_size_, &safe_stack_);

#if __has_feature(shadow_call_stack)
  AllocateASAN(kShadowCallStackSize, &shadow_call_stack_);
#endif
}

Stack::~Stack() {
  ReleaseASAN(safe_stack_);

#if __has_feature(shadow_call_stack)
  ReleaseASAN(shadow_call_stack_);
#endif
}

void Stack::Release() {
  ReleaseASAN(safe_stack_);
  AllocateASAN(stack_size_, &safe_stack_);

#if __has_feature(shadow_call_stack)
  ReleaseASAN(shadow_call_stack_);
  AllocateASAN(kShadowCallStackSize, &shadow_call_stack_);
#endif
}

#else  // __has_feature(address_sanitizer)

Stack::Stack(size_t stack_size) : stack_size_(ToFullPages(stack_size)) {
  LEDGER_DCHECK(stack_size_);
  zx_status_t status = zx::vmo::create(VmoSize(stack_size_), 0, &vmo_);
  LEDGER_DCHECK(status == ZX_OK);

  AllocateStack(vmo_, 0, stack_size_, &safe_stack_mapping_, &safe_stack_);
  LEDGER_DCHECK(safe_stack_);

#if __has_feature(safe_stack)
  AllocateStack(vmo_, stack_size_, stack_size_, &unsafe_stack_mapping_, &unsafe_stack_);
  LEDGER_DCHECK(unsafe_stack_);
#endif

#if __has_feature(shadow_call_stack)
  AllocateStack(vmo_, stack_size_ * kVmoSizeMultiplier, kShadowCallStackSize,
                &shadow_call_stack_mapping_, &shadow_call_stack_);
  LEDGER_DCHECK(shadow_call_stack_);
#endif
}

Stack::~Stack() {
  safe_stack_mapping_.destroy();
#if __has_feature(safe_stack)
  unsafe_stack_mapping_.destroy();
#endif
#if __has_feature(shadow_call_stack)
  shadow_call_stack_mapping_.destroy();
#endif
}

void Stack::Release() {
  zx_status_t status = vmo_.op_range(ZX_VMO_OP_DECOMMIT, 0, VmoSize(stack_size_), nullptr, 0);
  LEDGER_DCHECK(status == ZX_OK);
}

#endif  // __has_feature(address_sanitizer)

}  // namespace context
