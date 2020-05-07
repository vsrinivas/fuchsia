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

#ifdef __x86_64__

inline constexpr size_t kAsanShift = 3;
inline constexpr size_t kAsanShadowSize = KERNEL_ASPACE_SIZE >> kAsanShift;

// Any value in the shadow above this value is poisoned.
inline constexpr uint8_t kAsanSmallestPoisonedValue = 0x08;

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

#endif  // __x86_64__

void arch_asan_reallocate_shadow(uintptr_t physmap_shadow_begin, uintptr_t physmap_shadow_end);

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_ASAN_ASAN_INTERNAL_H_
