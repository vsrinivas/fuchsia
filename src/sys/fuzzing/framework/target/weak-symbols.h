// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TARGET_WEAK_SYMBOLS_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TARGET_WEAK_SYMBOLS_H_

#include <stddef.h>

// The symbols in this file match those defined in several header files in LLVM, under
// compiler-rt/include/sanitizer. The only change here is to define them as weak, and thereby defer
// the determination of which sanitizer is present until runtime.
//
// Linting is disabled as clang-tidy raises several complaints about these symbols, e.g.
// "bugprone-reserved-identifier".

#define WEAK_EXPORT __attribute__((weak, visibility("default")))

extern "C" {

// From compiler-rt/include/sanitizer/common_interface_defs.h
WEAK_EXPORT int __sanitizer_acquire_crash_state();                        // NOLINT
WEAK_EXPORT void __sanitizer_print_memory_profile(size_t, size_t);        // NOLINT
WEAK_EXPORT void __sanitizer_set_death_callback(void (*callback)(void));  // NOLINT

// From compiler-rt/include/sanitizer/allocator_interface.h
WEAK_EXPORT int __sanitizer_install_malloc_and_free_hooks(
    void (*malloc_hook)(const volatile void *, size_t),
    void (*free_hook)(const volatile void *));   // NOLINT
WEAK_EXPORT void __sanitizer_purge_allocator();  // NOLINT

// From compiler-rt/include/sanitizer/lsan_interface.h
WEAK_EXPORT void __lsan_enable();                    // NOLINT
WEAK_EXPORT void __lsan_disable();                   // NOLINT
WEAK_EXPORT int __lsan_do_recoverable_leak_check();  // NOLINT

}  // extern "C"

#undef WEAK_EXPORT

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TARGET_WEAK_SYMBOLS_H_
