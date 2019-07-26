// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_AFFINE_ASSERT_H_
#define LIB_AFFINE_ASSERT_H_

#include <zircon/assert.h>

// This library needs to be used in places where we don't want to have to depend
// on the C standard library, but we still want to have ASSERT and
// DEBUG_ASSERTs.  The zircon implementations of these macros currently depend
// on printf(...) and abort().  Introduce simplified implementations which do
// not print, and which use the __builtin_trap() intrinsic when the ASSERT fails
// instead of using the more standard zircon asserts.
//
namespace affine {
namespace internal {

inline void Assert(bool predicate) {
  if (!predicate) {
    // TODO(johngro): add some guarantee that this will terminate the
    // process when possible.
    //
    // Right now, we want to be able to use this code in...
    //
    // 1) Normal places where an assert could have been used anyway.
    // 2) In special, low-level user mode code places where not all of libc
    //    may be available.
    // 3) In the kernel, where syscalls like zx_process_exit, may not be
    //    available.
    //
    // so, simply adding a call to zx_process_exit (like the MUSL
    // implementation of abort does) is not a simple option.
    __builtin_trap();
  }
}

inline void DebugAssert(bool predicate) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  Assert(predicate);
#endif
}

}  // namespace internal
}  // namespace affine

#endif
