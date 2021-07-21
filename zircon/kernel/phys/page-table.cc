// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/allocator.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>

#include <ktl/byte.h>
#include <phys/page-table.h>

ktl::byte* AllocationMemoryManager::Allocate(size_t size, size_t alignment) {
  auto result = pool_.Allocate(memalloc::Type::kIdentityPageTables, size, alignment);
  if (result.is_error()) {
    return nullptr;
  }
  return reinterpret_cast<ktl::byte*>(static_cast<uintptr_t>(result.value()));
}
