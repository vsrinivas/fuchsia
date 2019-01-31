// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asan_impl.h"

// In the ASan build, this file provides weak definitions for all the
// same entry points that are defined by the ASan runtime library.
// The definitions here are stubs that are used only during the
// dynamic linker's startup phase before the ASan runtime shared
// library has been loaded.  These are required to satisfy the
// references in libc's own code.
//
// LLVM provides no documentation on the ABI between the compiler and
// the runtime.  The set of function signatures here was culled from
// the LLVM sources for the compiler instrumentation and the runtime
// (see llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp and
// compiler-rt/lib/asan/*).

#if __has_feature(address_sanitizer)

// This is referenced by generated code to decide whether to call
// __asan_stack_malloc_* instead of doing normal stack allocation.
// Never use stack malloc before the real runtime library is loaded.
__WEAK const int __asan_option_detect_stack_use_after_return = 0;

// This is the one set of things we define for real just as the
// sanitizer runtime does.  Generated code calls these.  In practice,
// almost certainly nothing in the the startup path needs them, but
// defining them properly is barely more than defining trap stubs.
#define ASAN_SET_SHADOW_XX(xx)                                          \
    __WEAK void __asan_set_shadow_##xx(uintptr_t addr, uintptr_t size) { \
        __unsanitized_memset((void*)addr, 0x##xx, size);                \
    }

ASAN_SET_SHADOW_XX(00)
ASAN_SET_SHADOW_XX(f1)
ASAN_SET_SHADOW_XX(f2)
ASAN_SET_SHADOW_XX(f3)
ASAN_SET_SHADOW_XX(f5)
ASAN_SET_SHADOW_XX(f8)

// Everything else is trap stubs.  They should never be called.

#define TRAP_STUB(decl) __WEAK decl { __builtin_trap(); }

// These are only called when a bug is found.  So unless there's
// an actual bug in code that's on the dynamic linker startup path,
// they'll never be called.

// This is the same macro used in compiler-rt/lib/asan/asan_rtl.cc,
// where it makes use of the is_write argument.  The list of invocations
// of this macro below is taken verbatim from that file.
#define ASAN_REPORT_ERROR(type, is_write, size)                         \
    TRAP_STUB(void __asan_report_##type##size(uintptr_t addr))          \
    TRAP_STUB(void __asan_report_exp_##type##size(uintptr_t addr,       \
                                                  uint32_t exp))        \
    TRAP_STUB(void __asan_report_##type##size##_noabort(uintptr_t addr))

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

TRAP_STUB(void __asan_report_load_n(uintptr_t addr, size_t size))
TRAP_STUB(void __asan_report_load_n_noabort(uintptr_t addr, size_t size))
TRAP_STUB(void __asan_report_exp_load_n(uintptr_t addr, size_t size,
                                        uint32_t exp))

TRAP_STUB(void __asan_report_store_n(uintptr_t addr, size_t size))
TRAP_STUB(void __asan_report_store_n_noabort(uintptr_t addr, size_t size))
TRAP_STUB(void __asan_report_exp_store_n(uintptr_t addr, size_t size,
                                         uint32_t exp))

// These are sometimes called in normal operation.  But they're never
// called by any of the code on the startup path, so we can get away
// with making them trap stubs.

TRAP_STUB(void __asan_handle_no_return(void))

#define DEFINE_STACK_MALLOC_FREE_WITH_CLASS_ID(class_id)                \
    TRAP_STUB(uintptr_t __asan_stack_malloc_##class_id(uintptr_t size)) \
    TRAP_STUB(void __asan_stack_free_##class_id(uintptr_t ptr, size_t size))

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

TRAP_STUB(void __asan_alloca_poison(uintptr_t addr, uintptr_t size))
TRAP_STUB(void __asan_allocas_unpoison(uintptr_t top, uintptr_t bottom))

// These are called to initialize the sanitizer runtime.  These will
// be needed for libc's and the dynamic linker's own code, but they
// won't be called until after the sanitizer runtime is loaded.  So
// these trap stubs just satisfy the references in libc's own code
// before other libraries are loaded, and ensure that they really
// don't get called too early.

TRAP_STUB(void __asan_init(void))
TRAP_STUB(void __asan_version_mismatch_check_v8(void))

TRAP_STUB(void __asan_register_globals(uintptr_t globals, size_t n))
TRAP_STUB(void __asan_unregister_globals(uintptr_t globals, size_t n))
TRAP_STUB(void __asan_register_elf_globals(uintptr_t flag,
                                           uintptr_t start, uintptr_t stop))
TRAP_STUB(void __asan_unregister_elf_globals(uintptr_t flag,
                                             uintptr_t start, uintptr_t stop))


#endif // __has_feature(address_sanitizer)
