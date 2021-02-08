// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "allocation.h"

#include <lib/arch/sysreg.h>
#include <lib/arch/x86/system.h>
#include <lib/memalloc.h>
#include <zircon/assert.h>

#include <fbl/no_destructor.h>
#include <ktl/byte.h>
#include <ktl/move.h>

#include "main.h"

namespace {

// Maximum number of address ranges we support reading from the
// previous-stage bootloader.
constexpr size_t kMaxMemoryRanges = 128;

// Remove architecture-specific regions of memory.
void ArchRemoveReservedRanges(memalloc::Allocator& allocator) {
#if defined(__x86_64__)
  // On x86_64, remove space likely to be holding our page tables. We assume
  // here that the page tables are contiguously allocated, starting at CR3,
  // and all fitting within 1MiB.
  //
  // TODO(fxb/67632): This is a temporary hack to make this work on x86.
  // Longer term, we plan to allocate new page tables and switch into those
  // instead of attempting to find the existing ones.
  //
  // TODO(fxb/67631): Move architecture-specific code into arch/ directories.
  {
    // Get top-level page directory location, stored in the CR3 register.
    uint64_t pt_base = arch::X86Cr3::Read().base();

    // Remove the range.
    constexpr uint64_t kMiB = 1024 * 1024;
    zx::status<> result = allocator.RemoveRange(pt_base, 1 * kMiB);
    ZX_ASSERT(result.is_ok());
  }

  // On x86-64, remove space unlikely to be mapped into our address space (anything past 1 GiB).
  constexpr uint64_t kGiB = 1024 * 1024 * 1024;
  zx::status<> result = allocator.RemoveRange(1 * kGiB, UINT64_MAX - 1 * kGiB + 1);
  ZX_ASSERT(result.is_ok());
#endif
}

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

  // Remove any arch-specific reserved ranges.
  ArchRemoveReservedRanges(allocator);
}
