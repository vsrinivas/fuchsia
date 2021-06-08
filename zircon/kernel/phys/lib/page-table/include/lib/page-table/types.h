// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_TYPES_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_TYPES_H_

#include <zircon/types.h>

#include <cstddef>

#include <fbl/strong_int.h>

namespace page_table {

// Virtual address.
DEFINE_STRONG_INT(Vaddr, uint64_t)

// Physical address.
DEFINE_STRONG_INT(Paddr, uint64_t)

// Interface abstracting away memory operations, such as mapping addresses
// to and from physical addresses.
class MemoryManager {
 public:
  // Get the physical address of the given pointer.
  virtual Paddr PtrToPhys(std::byte* ptr) = 0;

  // Get a pointer to the given physical address.
  virtual std::byte* PhysToPtr(Paddr phys) = 0;

  // Allocate memory with the given size / alignment.
  //
  // Return nullptr if allocation failed.
  virtual std::byte* Allocate(size_t size, size_t alignment) = 0;
};

// Allow addition / subtraction of offsets with type `size_t`, so expressions
// such as the following work as expected:
//
//     Vaddr x = ...;
//     x += sizeof(int);
//
#define DEFINE_OP(type, op)                                                            \
  /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
  constexpr type& operator op##=(type& self, size_t offset) {                          \
    self op## = type(offset);                                                          \
    return self;                                                                       \
  }                                                                                    \
  /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
  constexpr type operator op(const type& lhs, size_t rhs) { return lhs op type(rhs); } \
  /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                     \
  constexpr type operator op(size_t lhs, const type& rhs) { return type(lhs) op rhs; }
DEFINE_OP(Vaddr, +)
DEFINE_OP(Vaddr, -)
DEFINE_OP(Paddr, +)
DEFINE_OP(Paddr, -)
#undef DEFINE_OP

// Caching attributes of memory.
//
// The following values will have architecture-specific interpretations.
enum class CacheAttributes {
  kNormal = 0,  // Normal, cached memory.
  kDevice = 1,  // Memory suitable for MMIO and communication with devices.
};

}  // namespace page_table

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_TYPES_H_
