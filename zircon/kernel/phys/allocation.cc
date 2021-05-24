// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/allocation.h"

#include <lib/arch/sysreg.h>
#include <lib/arch/x86/system.h>
#include <lib/memalloc/allocator.h>
#include <zircon/assert.h>

#include <fbl/no_destructor.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <phys/main.h>

namespace {

// Maximum number of discontiguous address ranges we support reading from the
// previous-stage bootloader.
constexpr size_t kMaxMemoryRanges = 512;

}  // namespace

// Global memory allocation book-keeping.
memalloc::Allocator& Allocation::GetAllocator() {
  static memalloc::RangeStorage allocator_storage[kMaxMemoryRanges];
  // Use fbl::NoDestructor to avoid generation of static destructors,
  // which fails in the phys environment.
  static fbl::NoDestructor<memalloc::Allocator> allocator(allocator_storage);
  return *allocator;
}

// This is where actual allocation happens.
// The returned object is default-constructed if it fails.
Allocation Allocation::New(fbl::AllocChecker& ac, size_t size, size_t alignment) {
  Allocation alloc;
  zx::status<uint64_t> result = GetAllocator().Allocate(size, alignment);
  ac.arm(size, result.is_ok());
  if (result.is_ok()) {
    alloc.data_ = {reinterpret_cast<ktl::byte*>(result.value()), size};
  }
  return alloc;
}

// This is where actual deallocation happens.  The destructor just calls this.
void Allocation::reset() {
  if (!data_.empty()) {
    auto result = GetAllocator().AddRange(reinterpret_cast<uint64_t>(data_.data()), data_.size());
    ZX_ASSERT(result.is_ok());
    data_ = {};
  }
}

void Allocation::InitReservedRanges() {
  auto& allocator = GetAllocator();

  // Remove our own load image from the range of usable memory.
  auto start = reinterpret_cast<uintptr_t>(&PHYS_LOAD_ADDRESS);
  auto end = reinterpret_cast<uintptr_t>(&_end);
  ZX_ASSERT(allocator.RemoveRange(start, /*size=*/end - start).is_ok());

  // Remove the bottom page, to avoid confusion with nullptr.
  ZX_ASSERT(allocator.RemoveRange(0, ZX_PAGE_SIZE).is_ok());
}
