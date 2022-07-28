// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/fdio_state.h"

// Constexpr function to force initialization at program load time. Otherwise initialization may
// occur after |__libc_extension_init|, wiping the fd table *after* it has been filled in with valid
// entries; musl invokes |__libc_start_init| after |__libc_extensions_init|.
//
// There are no language guarantees here. C++20 provides the constinit specifier, and clang provides
// the [[clang::require_constant_initialization]] attribute, but until we are on C++20 this is the
// best we can do.
//
// Note that even moving the initialization to |__libc_extensions_init| doesn't work out in the
// presence of sanitizers that deliberately initialize with garbage *after* |__libc_extensions_init|
// runs.
fdio_state_t __fdio_global_state = []() constexpr {
  return fdio_state_t{
      .cwd_path = fdio_internal::PathBuffer('/'),
  };
}
();
