// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "bump_allocator.h"

#include <zircon/assert.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

static constexpr size_t AllocAlign(size_t n) {
  return ((n + alignof(std::max_align_t) - 1) & -alignof(std::max_align_t));
}

static_assert(AllocAlign(0) == 0u);
static_assert(AllocAlign(1) == 16u);
static_assert(AllocAlign(17) == 32u);

BumpAllocator::BumpAllocator(const zx::vmar* vmar) : mapper_(vmar) {}

BumpAllocator::~BumpAllocator() = default;

zx_status_t BumpAllocator::Init(size_t heap_size) {
  if (heap_ != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status = zx::vmo::create(heap_size, 0u, &vmo_);
  if (status != ZX_OK) {
    return status;
  }

  status = mapper_.Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, vmo_, 0u, heap_size);
  if (status != ZX_OK) {
    return status;
  }
  heap_ = mapper_.data();
  heap_size_ = heap_size;
  return ZX_OK;
}

void* BumpAllocator::malloc(size_t n) {
  n = AllocAlign(n);
  if (n < heap_size_ - frontier_) {
    last_block_ = &heap_[frontier_];
    frontier_ += n;
    return last_block_;
  }
  __builtin_trap();
  return nullptr;
}

void BumpAllocator::free(void* ptr) {}
