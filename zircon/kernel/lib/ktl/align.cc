// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>

#include <ktl/align.h>
#include <ktl/byte.h>

// This has to be defined as std:: rather than ktl:: because we are providing a
// definition to replace libc++'s, to which ktl::align is aliased.
void* std::align(size_t alignment, size_t size, void*& ptr, size_t& space) {
  if (size > space) {
    return nullptr;
  }
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t aligned_addr = (addr + alignment - 1) & -alignment;
  size_t skipped = aligned_addr - addr;
  if (skipped > space - size) {
    return nullptr;
  }
  ptr = reinterpret_cast<void*>(aligned_addr);
  space -= skipped;
  return ptr;
}
