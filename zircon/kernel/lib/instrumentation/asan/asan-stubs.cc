// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <platform.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/span.h>
#include <sanitizer/asan_interface.h>
#include <vm/pmm.h>

#include "asan-internal.h"

// LLVM provides no documentation on the ABI between the compiler and
// the runtime.  The set of function signatures here was culled from
// the LLVM sources for the compiler instrumentation and the runtime
// (see llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp and
// compiler-rt/lib/asan/*).

namespace {

constexpr size_t kAsanGranularity = (1 << kAsanShift);
constexpr size_t kAsanGranularityMask = kAsanGranularity - 1;

// Checks whether a memory |address| is poisoned.
//
// ASAN tracks address poison status at byte granularity in a shadow map.
// kAsanGranularity bytes are represented by one byte in the shadow map.
//
// If the value in the shadow map is 0, accesses to address are allowed.
// If the value is between 1 and kAsanGranularity, accesses to the corresponding
// addresses less than the value are allowed.
// All other values disallow access to the entire aligned region.
bool is_byte_poisoned(uintptr_t address) {
  const uint8_t shadow_val = *addr2shadow(address);
  // Zero values in the shadow map mean that all 8 bytes are valid.
  if (shadow_val == 0) {
    return false;
  }
  if (shadow_val >= kAsanSmallestPoisonedValue) {
    return true;
  }
  // Part of this region is poisoned. Check whether address is below the last valid byte.
  const size_t offset = address & kAsanGranularityMask;
  return shadow_val <= offset;
}

// Checks if an entire memory region is all zeroes.
bool is_mem_zero(ktl::span<const uint8_t> region) {
  for (auto val : region) {
    if (val != 0) {
      return false;
    }
  }
  return true;
}

uintptr_t is_region_poisoned(uintptr_t address, size_t size) {
  const uintptr_t end = address + size;
  const uintptr_t aligned_begin = ROUNDUP(address, kAsanGranularity);
  const uintptr_t aligned_end = ROUNDDOWN(end, kAsanGranularity);
  const uint8_t* const shadow_beg = addr2shadow(aligned_begin);
  const uint8_t* const shadow_end = addr2shadow(aligned_end);

  if (!is_byte_poisoned(address) && !is_byte_poisoned(end - 1) &&
      (shadow_end <= shadow_beg ||
       is_mem_zero({shadow_beg, static_cast<size_t>(shadow_end - shadow_beg)}))) {
    return 0;
  }

  for (size_t i = 0; i < size; i++) {
    if (is_byte_poisoned(address + i)) {
      return address + i;
    }
  }
  panic("Unreachable code\n");
}

// When kASAN has detected an invalid access, print information about the access and the
// corresponding parts of the shadow map. Also print PMM page state.
//
// Example:
// (Shadow address)        (shadow map contents)
//
// KASAN detected an error: ptr={{{data:0xffffff8043251830}}}, size=0x4, caller:
// {{{pc:0xffffffff001d9371}}} Shadow memory state around the buggy address 0xffffffe00864a306:
// 0xffffffe00864a2f0: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a2f8: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a300: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
//                                                    ^^
// 0xffffffe00864a308: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// 0xffffffe00864a310: 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa 0xfa
// page 0xffffff807f475f30: address 0x43251000 state heap flags 0
void print_error_shadow(uintptr_t address, size_t bytes, void* caller, uintptr_t poisoned_addr) {
  const uintptr_t shadow = reinterpret_cast<uintptr_t>(addr2shadow(address));

  dprintf(CRITICAL,
          "\nKASAN detected an error: ptr={{{data:%#lx}}}, size=%#zx, caller: {{{pc:%p}}}\n",
          address, bytes, caller);

  // TODO(30033): Decode the shadow value into 'use-after-free'/redzone/page free/etc.
  printf("Shadow memory state around the buggy address %#lx:\n", shadow);
  // Print at least 16 bytes of the shadow map before and after the invalid access.
  uintptr_t start_addr = (shadow & ~0x07) - 0x10;
  start_addr = ktl::max(KASAN_SHADOW_OFFSET, start_addr);
  // Print the shadow map memory state and look for the location to print a caret.
  bool caret = false;
  size_t caret_ind = 0;
  for (size_t i = 0; i < 5; i++) {
    // TODO(fxb/51170): When kernel printf properly supports #, switch.
    printf("0x%016lx:", start_addr);
    for (size_t j = 0; j < 8; j++) {
      printf(" 0x%02hhx", reinterpret_cast<uint8_t*>(start_addr)[j]);
      if (!caret) {
        if ((start_addr + j) == reinterpret_cast<uintptr_t>(addr2shadow(poisoned_addr))) {
          caret = true;
          caret_ind = j;
        }
      }
    }
    printf("\n");
    if (caret) {
      // The address takes 16 characters; add in space for ':', and "0x".
      printf("%*s", 16 + 1 + 2, "");
      // Print either a caret or spaces under the line containing the invalid access.
      for (size_t j = 0; j < 8; j++) {
        printf("  %2s ", j == caret_ind ? "^^" : "");
      }
      printf("\n");
      caret = false;
    }
    start_addr += 8;
  }
  // Dump additional VM Page state - this is useful to debug use-after-state-change bugs.
  paddr_to_vm_page(vaddr_to_paddr(reinterpret_cast<void*>(address)))->dump();
}

void asan_check(uintptr_t address, size_t bytes, void* caller) {
  // TODO(30033): Inline the fast path for constant-size checks.
  const uintptr_t poisoned_addr = is_region_poisoned(address, bytes);
  if (!poisoned_addr) {
    return;
  }
  platform_panic_start();
  print_error_shadow(address, bytes, caller, poisoned_addr);
  panic("kasan\n");
}

}  // anonymous namespace

extern "C" {

extern decltype(memcpy) __unsanitized_memcpy;
extern decltype(memset) __unsanitized_memset;

void* __asan_memcpy(void* dst, const void* src, size_t n) {
  asan_check(reinterpret_cast<uintptr_t>(src), n, __builtin_return_address(0));
  asan_check(reinterpret_cast<uintptr_t>(dst), n, __builtin_return_address(0));
  return __unsanitized_memcpy(dst, src, n);
}

void* __asan_memset(void* dst, int c, size_t n) {
  asan_check(reinterpret_cast<uintptr_t>(dst), n, __builtin_return_address(0));
  return __unsanitized_memset(dst, c, n);
}

// This is referenced by generated code to decide whether to call
// __asan_stack_malloc_* instead of doing normal stack allocation.
// Never use stack malloc before the real runtime library is loaded.
extern const int __asan_option_detect_stack_use_after_return = 0;

// This is the one set of things we define for real just as the
// sanitizer runtime does.  Generated code calls these.  In practice,
// almost certainly nothing in the the startup path needs them, but
// defining them properly is barely more than defining trap stubs.
#define ASAN_SET_SHADOW_XX(xx) \
  void __asan_set_shadow_##xx(uintptr_t addr, uintptr_t size) { memset((void*)addr, 0x##xx, size); }

ASAN_SET_SHADOW_XX(00)
ASAN_SET_SHADOW_XX(f1)
ASAN_SET_SHADOW_XX(f2)
ASAN_SET_SHADOW_XX(f3)
ASAN_SET_SHADOW_XX(f5)
ASAN_SET_SHADOW_XX(f8)

// Everything else is stubs that panic.  They should never be called.

#define PANIC_STUB(decl) \
  decl { ZX_PANIC("address sanitizer failure (%s)", __func__); }

// These are only called when a bug is found.  So unless there's
// an actual bug in code that's on the dynamic linker startup path,
// they'll never be called.

// This is the same macro used in compiler-rt/lib/asan/asan_rtl.cc,
// where it makes use of the is_write argument.  The list of invocations
// of this macro below is taken verbatim from that file.
#define ASAN_REPORT_ERROR(type, is_write, size)                                 \
  PANIC_STUB(void __asan_report_##type##size(uintptr_t addr))                   \
  PANIC_STUB(void __asan_report_exp_##type##size(uintptr_t addr, uint32_t exp)) \
  PANIC_STUB(void __asan_report_##type##size##_noabort(uintptr_t addr))

ASAN_REPORT_ERROR(load, false, 1)
ASAN_REPORT_ERROR(load, false, 2)
ASAN_REPORT_ERROR(load, false, 4)
ASAN_REPORT_ERROR(load, false, 8)
ASAN_REPORT_ERROR(load, false, 16)
ASAN_REPORT_ERROR(store, true, 1)
ASAN_REPORT_ERROR(store, true, 2)
ASAN_REPORT_ERROR(store, true, 4)
ASAN_REPORT_ERROR(store, true, 8)
ASAN_REPORT_ERROR(store, true, 16)

PANIC_STUB(void __asan_report_load_n(uintptr_t addr, size_t size))
PANIC_STUB(void __asan_report_load_n_noabort(uintptr_t addr, size_t size))
PANIC_STUB(void __asan_report_exp_load_n(uintptr_t addr, size_t size, uint32_t exp))

PANIC_STUB(void __asan_report_store_n(uintptr_t addr, size_t size))
PANIC_STUB(void __asan_report_store_n_noabort(uintptr_t addr, size_t size))
PANIC_STUB(void __asan_report_exp_store_n(uintptr_t addr, size_t size, uint32_t exp))

// These are called when not using the inline instrumentation that calls the
// ASAN_REPORT_ERROR functions for poisoned accesses.  Instead, calls to these
// functions are generated unconditionally before an access to perform the
// poison check.

void __asan_loadN(uintptr_t addr, size_t size) {
  asan_check(addr, size, __builtin_return_address(0));
}
void __asan_storeN(uintptr_t addr, size_t size) {
  asan_check(addr, size, __builtin_return_address(0));
}

// This is the same macro used in compiler-rt/lib/asan/asan_rtl.cc,
// where it makes use of the is_write argument.  The list of invocations
// of this macro below is taken verbatim from that file.
#define ASAN_MEMORY_ACCESS_CALLBACK(type, is_write, size)      \
  void __asan_##type##size(uintptr_t addr) {                   \
    asan_check(addr, size, __builtin_return_address(0));       \
  }                                                            \
  void __asan_exp_##type##size(uintptr_t addr, uint32_t exp) { \
    asan_check(addr, size, __builtin_return_address(0));       \
  }

ASAN_MEMORY_ACCESS_CALLBACK(load, false, 1)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 2)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 4)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 8)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 16)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 1)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 2)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 4)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 8)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 16)

// This is called before calling any [[noreturn]] function.  In the userland
// runtime, it's used to clean up per-thread "fake stack" allocations.  In the
// kernel, all per-thread cleanup is done explicitly.
void __asan_handle_no_return() {}

// These are called in normal operation when using arrays.
void __asan_poison_cxx_array_cookie(uintptr_t p) {}
uintptr_t __asan_load_cxx_array_cookie(uintptr_t* p) { return *p; }

// These are sometimes called in normal operation.  But they're never
// called by any of the code on the startup path, so we can get away
// with making them trap stubs.

#define DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(class_id)               \
  PANIC_STUB(uintptr_t __asan_stack_malloc_##class_id(uintptr_t size)) \
  PANIC_STUB(void __asan_stack_free_##class_id(uintptr_t ptr, size_t size))

DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(0)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(1)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(2)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(3)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(4)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(5)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(6)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(7)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(8)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(9)
DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(10)

PANIC_STUB(void __asan_alloca_poison(uintptr_t addr, uintptr_t size))
PANIC_STUB(void __asan_allocas_unpoison(uintptr_t top, uintptr_t bottom))

// These are called by static constructor code to initialize the sanitizer
// runtime.  There's no need for those calls in the kernel, since the
// initialization is all done explicitly.
void __asan_init() {}
void __asan_version_mismatch_check_v8() {}

}  // extern "C"
