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

constexpr size_t kAsanMaxGlobalsRegions = 400;
static asan_global* g_globals_regions[kAsanMaxGlobalsRegions];
static size_t g_globals_regions_sizes[kAsanMaxGlobalsRegions];
static size_t g_total_globals;

extern "C" {

void* __asan_memcpy(void* dst, const void* src, size_t n) {
  if (n == 0)
    return dst;
  auto dstptr = reinterpret_cast<uintptr_t>(dst);
  auto srcptr = reinterpret_cast<uintptr_t>(src);

  asan_check_memory_overlap(dstptr, n, srcptr, n);
  asan_check(srcptr, n, /*is_write=*/false, __builtin_return_address(0));
  asan_check(dstptr, n, /*is_write=*/true, __builtin_return_address(0));
  return __unsanitized_memcpy(dst, src, n);
}

void* __asan_memset(void* dst, int c, size_t n) {
  if (n == 0)
    return dst;
  asan_check(reinterpret_cast<uintptr_t>(dst), n, /*is_write=*/true, __builtin_return_address(0));
  return __unsanitized_memset(dst, c, n);
}

void* __asan_memmove(void* dst, const void* src, size_t n) {
  if (n == 0)
    return dst;
  asan_check(reinterpret_cast<uintptr_t>(src), n, /*is_write=*/false, __builtin_return_address(0));
  asan_check(reinterpret_cast<uintptr_t>(dst), n, /*is_write=*/true, __builtin_return_address(0));
  return __unsanitized_memmove(dst, src, n);
}

decltype(__asan_memcpy) memcpy [[gnu::alias("__asan_memcpy")]];
decltype(__asan_memmove) memmove [[gnu::alias("__asan_memmove")]];
decltype(__asan_memset) memset [[gnu::alias("__asan_memset")]];

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
#define ASAN_REPORT_ERROR(type, is_write, size)                       \
  void __asan_report_##type##size(uintptr_t addr) {                   \
    asan_check(addr, size, is_write, __builtin_return_address(0));    \
  }                                                                   \
  void __asan_report_exp_##type##size(uintptr_t addr, uint32_t exp) { \
    asan_check(addr, size, is_write, __builtin_return_address(0));    \
  }                                                                   \
  void __asan_report_##type##size##_noabort(uintptr_t addr) {         \
    asan_check(addr, size, is_write, __builtin_return_address(0));    \
  }

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

void __asan_report_load_n(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/false, __builtin_return_address(0));
}
void __asan_report_load_n_noabort(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/false, __builtin_return_address(0));
}
void __asan_report_exp_load_n(uintptr_t addr, size_t size, uint32_t exp) {
  asan_check(addr, size, /*is_write=*/false, __builtin_return_address(0));
}
void __asan_report_store_n(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/true, __builtin_return_address(0));
}
void __asan_report_store_n_noabort(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/true, __builtin_return_address(0));
}
void __asan_report_exp_store_n(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/true, __builtin_return_address(0));
}

// These are called when not using the inline instrumentation that calls the
// ASAN_REPORT_ERROR functions for poisoned accesses.  Instead, calls to these
// functions are generated unconditionally before an access to perform the
// poison check.

void __asan_loadN(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/false, __builtin_return_address(0));
}
void __asan_storeN(uintptr_t addr, size_t size) {
  asan_check(addr, size, /*is_write=*/true, __builtin_return_address(0));
}

// This is the same macro used in compiler-rt/lib/asan/asan_rtl.cc,
// where it makes use of the is_write argument.  The list of invocations
// of this macro below is taken verbatim from that file.
#define ASAN_MEMORY_ACCESS_CALLBACK(type, is_write, size)          \
  void __asan_##type##size(uintptr_t addr) {                       \
    asan_check(addr, size, is_write, __builtin_return_address(0)); \
  }                                                                \
  void __asan_exp_##type##size(uintptr_t addr, uint32_t exp) {     \
    asan_check(addr, size, is_write, __builtin_return_address(0)); \
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

void __asan_register_globals(asan_global* globals, size_t size) {
  if (g_total_globals == kAsanMaxGlobalsRegions) {
    // This will fail in asan_register_globals_late.
    return;
  }
  g_globals_regions[g_total_globals] = globals;
  g_globals_regions_sizes[g_total_globals] = size;
  g_total_globals++;
}

void __asan_unregister_globals(asan_global* globals, size_t size) {
  ZX_PANIC("__asan_unregister_globals should be unreachable code");
}

void asan_register_globals_late() {
  DEBUG_ASSERT(g_total_globals < kAsanMaxGlobalsRegions);
  for (size_t i = 0; i < g_total_globals; i++) {
    asan_global* region = g_globals_regions[i];
    for (size_t j = 0; j < g_globals_regions_sizes[i]; j++) {
      asan_global* g = &region[j];
      asan_poison_shadow((reinterpret_cast<uintptr_t>(g->begin) + g->size),
                         g->size_with_redzone - g->size, kAsanGlobalRedzoneMagic);
    }
  }
}

// TODO(fxbug.dev/30033): Figure out what dynamic_init is doing.
void __asan_before_dynamic_init(const char* module) {}
void __asan_after_dynamic_init() {}

// These are called by static constructor code to initialize the sanitizer
// runtime.  There's no need for those calls in the kernel, since the
// initialization is all done explicitly.
void __asan_init() {}
void __asan_version_mismatch_check_v8() {}

}  // extern "C"
