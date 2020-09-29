// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_range.h"

#include <lib/zx/status.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace hwstress {

zx::status<std::unique_ptr<MemoryRange>> MemoryRange::Create(uint64_t size, CacheMode mode) {
  ZX_ASSERT(size % ZX_PAGE_SIZE == 0);

  // Create the VMO.
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, /*options=*/0, &vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Set memory mode of the uncached VMO.
  if (mode == CacheMode::kUncached) {
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }

  // Map the VMOs into memory.
  zx_vaddr_t addr;
  status = zx::vmar::root_self()->map(
      /*vmar_offset=*/0, vmo, /*vmo_offset=*/0, /*len=*/size,
      /*options=*/(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE), &addr);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(
      std::make_unique<MemoryRange>(std::move(vmo), reinterpret_cast<uint8_t*>(addr), size, mode));
}

MemoryRange::MemoryRange(zx::vmo vmo, uint8_t* addr, uint64_t size, CacheMode mode)
    : vmo_(std::move(vmo)), addr_(addr), size_(size), cache_mode_(mode) {
  ZX_ASSERT(reinterpret_cast<uintptr_t>(addr) % ZX_PAGE_SIZE == 0);
  ZX_ASSERT(size % ZX_PAGE_SIZE == 0);
}

void MemoryRange::DoCacheOp(uint32_t operation) {
  ZX_ASSERT(vmo_.op_range(operation, /*offset=*/0, /*size=*/size_,
                          /*buffer=*/nullptr,
                          /*buffer_size=*/0) == ZX_OK);
}

void MemoryRange::CleanCache() { DoCacheOp(ZX_VMO_OP_CACHE_CLEAN); }

void MemoryRange::CleanInvalidateCache() { DoCacheOp(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE); }

MemoryRange::~MemoryRange() {
  ZX_ASSERT(zx::vmar::root_self()->unmap(reinterpret_cast<zx_vaddr_t>(addr_), size_) == ZX_OK);
}

}  // namespace hwstress
