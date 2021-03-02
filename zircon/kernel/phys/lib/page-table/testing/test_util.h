// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_TESTING_TEST_UTIL_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_TESTING_TEST_UTIL_H_

#include <lib/page-table/types.h>
#include <zircon/assert.h>

#include <vector>

namespace page_table {

// Return a "physical address" (actually just the virtual address) of the given object.
template <typename T>
constexpr Paddr PaddrOf(T* object) {
  return Paddr(reinterpret_cast<uint64_t>(object));
}

// An allocator that just uses new/delete to allocate, and assumes a 1:1
// mapping from physical addresses to host virtual addresses.
class TestMemoryManager final : public MemoryManager {
 public:
  std::byte* Allocate(size_t size, size_t alignment) final {
    // Allocate aligned memory.
    void* result;
    if (int error = posix_memalign(&result, alignment, size); error != 0) {
      return nullptr;
    }
    ZX_ASSERT(result != nullptr);

    // Track the allocation.
    allocations_.push_back(result);

    return static_cast<std::byte*>(result);
  }

  Paddr PtrToPhys(std::byte* ptr) final { return PaddrOf(ptr); }

  std::byte* PhysToPtr(Paddr phys) final { return reinterpret_cast<std::byte*>(phys.value()); }

  ~TestMemoryManager() {
    for (void* allocation : allocations_) {
      free(allocation);
    }
  }

 private:
  // Tracks allocations so that we can free them when the test finishes.
  std::vector<void*> allocations_;
};

}  // namespace page_table

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_TESTING_TEST_UTIL_H_
