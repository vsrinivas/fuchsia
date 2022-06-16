// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/unique-backtrace.h>

#include "hwasan_impl.h"

// In the HWASan build, this file provides weak definitions for all the
// same entry points that are defined by the HWASan runtime library.
// The definitions here are stubs that are used only during the
// dynamic linker's startup phase before the HWASan runtime shared
// library has been loaded.  These are required to satisfy the
// references in libc's own code.
//
// LLVM provides no documentation on the ABI between the compiler and
// the runtime.  The set of function signatures here was culled from
// the LLVM sources for the compiler instrumentation and the runtime
// (see llvm/lib/Transforms/Instrumentation/HWAddressSanitizer.cpp and
// compiler-rt/lib/hwasan/*).

#if __has_feature(hwaddress_sanitizer)

// These should never actually be called until the hwasan runtime is loaded.
#define HWASAN_TRAP_STUB(name)                                           \
  extern "C" __EXPORT __WEAK __NO_SAFESTACK void __hwasan_##name(void) { \
    CRASH_WITH_UNIQUE_BACKTRACE();                                       \
  }
HWASAN_TRAP_STUB(init)
HWASAN_TRAP_STUB(storeN)
HWASAN_TRAP_STUB(store1)
HWASAN_TRAP_STUB(store2)
HWASAN_TRAP_STUB(store4)
HWASAN_TRAP_STUB(store8)
HWASAN_TRAP_STUB(store16)
HWASAN_TRAP_STUB(loadN)
HWASAN_TRAP_STUB(load1)
HWASAN_TRAP_STUB(load2)
HWASAN_TRAP_STUB(load4)
HWASAN_TRAP_STUB(load8)
HWASAN_TRAP_STUB(load16)
HWASAN_TRAP_STUB(tag_mismatch_v2)

#endif  // __has_feature(hwaddress_sanitizer)
