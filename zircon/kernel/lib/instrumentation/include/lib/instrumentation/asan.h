// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_

#ifdef __x86_64__

#define X86_KERNEL_KASAN_PDP_ENTRIES (64)
#define KASAN_SHADOW_OFFSET (0xffffffe000000000UL)

#endif  // __x86_64__

#ifdef __clang__
#define NO_ASAN [[clang::no_sanitize("address")]]
#else
#define NO_ASAN __attribute__((no_sanitize_address))
#endif

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

// ASAN dynamic poison interface - allows caller to "poison" or "unpoison" a region of kernel
// virtual addresses. Accesses to poisoned memory are invalid and may cause a fault or asan
// instrumentation check.
// This interface corresponds to the one in llvm compiler-rt/lib/asan/asan_interface.h
// It differs because we allow callers of asan_poison_shadow to specify a poison
// value.

// asan_poison_shadow() marks [address, address+size) with a one-byte |value|
// |value| == 0 "unpoisons" a region
// |value| [1, kAsanGranularity) are used for internal bookkeeping.
// All other |value|s can be used to annotate what the region's "type" is. For example,
//   one distinguished value is used for malloc metadata (kAsanHeapLeftRedzoneMagic).
void asan_poison_shadow(uintptr_t address, size_t size, uint8_t value);

// ASAN dynamic check functions - allows callers to check if an access would be valid without
// doing the access (aka poisoned). External accesses to a poisoned address is invalid and
// may cause a fault.
//
// Return true if any byte [|address|, |address + size|) is poisoned.
uintptr_t asan_region_is_poisoned(uintptr_t address, size_t size);
// Return true if kernel |address| is poisoned.
bool asan_address_is_poisoned(uintptr_t address);

// Distinguished kasan poison values.
// LLVM defines userspace equivalents of these in compiler-rt/lib/asan/asan_internal.h
// There are some differences - kernel ASAN has distinguished states for Pmm free, for example.

// These constants are reserved by the compiler for stack poisoning.
inline constexpr uint8_t kAsanStackLeftRedzoneMagic = 0xf1;
inline constexpr uint8_t kAsanStackMidRedzoneMagic = 0xf2;
inline constexpr uint8_t kAsanStackRightRedzoneMagic = 0xf3;
inline constexpr uint8_t kAsanStackAfterReturnMagic = 0xf5;
inline constexpr uint8_t kAsanStackUseAfterScopeMagic = 0xf8;

// These constants are only known to the asan runtime.
inline constexpr uint8_t kAsanArrayCookie = 0xac;
inline constexpr uint8_t kAsanGlobalRedzoneMagic = 0xf9;
inline constexpr uint8_t kAsanHeapLeftRedzoneMagic = 0xfa;
inline constexpr uint8_t kAsanPmmFreeMagic = 0xfb;
inline constexpr uint8_t kAsanQuarantineMagic = 0xfc;
inline constexpr uint8_t kAsanHeapFreeMagic = 0xfd;
inline constexpr uint8_t kAsanRedZone = 0xfe;
inline constexpr uint8_t kAsanAllocHeader = 0xff;

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_ASAN_H_
