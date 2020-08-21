// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_

#include <lib/instrumentation/asan.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/kernel_aspace.h>
#include <ktl/atomic.h>
#include <vm/physmap.h>

#ifdef __x86_64__

inline constexpr size_t kAsanShift = ASAN_MAPPING_SCALE;
inline constexpr size_t kAsanShadowSize = KERNEL_ASPACE_SIZE >> kAsanShift;
static_assert(X86_KERNEL_KASAN_PDP_ENTRIES * 1024ul * 1024ul * 1024ul == kAsanShadowSize);

inline constexpr size_t kAsanGranularity = (1 << kAsanShift);
inline constexpr size_t kAsanGranularityMask = kAsanGranularity - 1;

extern ktl::atomic<bool> g_asan_initialized;

// The redzone is an area of poisoned bytes added at the end of memory allocations. This allows
// detecting out-of-bounds accesses.
//
// Increasing this size allows detecting out-of-bounds access that are further beyond the end of
// the allocation, but each allocation would take more space.
//
// The kernel's implementation uses a fixed redzone plus a small variable block for alignment.
// In LLVM (compiler-rt)'s implementation of the asan runtime, the redzone is adaptive depending
// on the size of the allocation.
inline constexpr size_t kHeapRightRedzoneSize = 16;

// Any value in the shadow equal to or above this value is poisoned.
inline constexpr uint8_t kAsanSmallestPoisonedValue = 0x08;

// The current implementation of asan only checks accesses within the physmap.
inline constexpr vaddr_t kAsanStartAddress = PHYSMAP_BASE;
inline constexpr vaddr_t kAsanEndAddress = PHYSMAP_BASE + PHYSMAP_SIZE;

static_assert(X86_KERNEL_KASAN_PDP_ENTRIES * 1024ul * 1024ul * 1024ul == kAsanShadowSize);

// Returns the address of the shadow byte corresponding to |address|.
static inline uint8_t* addr2shadow(uintptr_t address) {
  DEBUG_ASSERT(address >= KERNEL_ASPACE_BASE);
  DEBUG_ASSERT(address <= KERNEL_ASPACE_BASE + KERNEL_ASPACE_SIZE - 1);

  uint8_t* const kasan_shadow_map = reinterpret_cast<uint8_t*>(KASAN_SHADOW_OFFSET);
  uint8_t* const shadow_byte_address =
      kasan_shadow_map + ((address - KERNEL_ASPACE_BASE) >> kAsanShift);
  return shadow_byte_address;
}

// Checks the validity of an entire region. This function panics and prints an
// error message if any part of [address, address+bytes) is poisoned.
void asan_check(uintptr_t address, size_t bytes, bool is_write, void* caller);

// Checks whether the two memory ranges defined by [offseta, offseta+lena) and
// [offsetb, offsetb + lenb) overlap. This function panics and prints an error message if
// the two memory ranges overlap.
void asan_check_memory_overlap(uintptr_t offseta, size_t lena, uintptr_t offsetb, size_t lenb);

#endif  // __x86_64__

// Structure shared between the compiler and ASAN runtime describing the location (in source code)
// where a particular global is defined.
//
// See LLVM compiler-rt/lib/asan/asan_interface_internal.h
struct asan_global_source_location {
  const char* filename;
  int line_no;
  int column_no;
};

// Structure shared between the compiler and ASAN runtime describing a global variable that is
// instrumented. Describes the virtual address, source location, size and redzone, and other
// metadata.
//
// See LLVM compiler-rt/lib/asan/asan_interface_internal.h
struct asan_global {
  const void* begin;
  size_t size;
  size_t size_with_redzone;
  const char* name;
  const char* module_name;
  uintptr_t dynamic_init;
  struct asan_global_source_location* asan_global_source_location;
  uintptr_t odr_indicator;
};

void arch_asan_reallocate_shadow();

extern "C" void asan_register_globals_late();

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_
