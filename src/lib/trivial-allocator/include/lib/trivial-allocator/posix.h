// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_POSIX_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_POSIX_H_

#include <sys/mman.h>
#include <unistd.h>

#include <cassert>
#include <utility>

namespace trivial_allocator {

// trivial::PosixMmap is default-constructible and uses mmap and mprotect to
// meet the Memory API for trivial_allocator::PageAllocator.
class PosixMmap {
 public:
  struct Capability {};

  [[gnu::const]] size_t page_size() const { return page_size_; }

  [[nodiscard]] std::pair<void*, Capability> Allocate(size_t size) {
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    return {result == MAP_FAILED ? nullptr : result, {}};
  }

  void Deallocate(Capability capability, void* ptr, size_t size) {
    [[maybe_unused]] int result = munmap(ptr, size);
    assert(result == 0);
  }

  void Seal(Capability capability, void* ptr, size_t size) {
    [[maybe_unused]] int result = mprotect(ptr, size, PROT_READ);
    assert(result == 0);
  }

 private:
  size_t page_size_ = sysconf(_SC_PAGE_SIZE);
};

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_POSIX_H_
