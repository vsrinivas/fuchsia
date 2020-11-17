// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "threads_impl.h"

// GWP_ASAN_PLATFORM_TLS_HEADER tells the gwp_asan sources to include this file
// and call this function instead of using a `thread_local` variable of its
// own.
//
// TODO(fxbug.dev/64175): Our current combined libc/dynamic linker
// implementation does not allow libc itself to have any `thread_local`
// variables of its own.  In future, a different dynamic linker implementation
// will likely remove this restriction and having gwp_asan use a (hidden
// visibility) `thread_local` variable will work fine.

namespace gwp_asan {

// This header is always included in a context where ThreadLocalPackedVariables
// has already been declared.

inline ThreadLocalPackedVariables* getThreadLocals() {
  auto* ptr = &__pthread_self()->gwp_asan_tsd;
  static_assert(sizeof(*ptr) >= sizeof(ThreadLocalPackedVariables));
  static_assert(alignof(decltype(*ptr)) >= alignof(ThreadLocalPackedVariables));
  return reinterpret_cast<ThreadLocalPackedVariables*>(ptr);
}

}  // namespace gwp_asan
