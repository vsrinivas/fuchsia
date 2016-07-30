// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/handles/unique_handle.h"

#include <magenta/syscalls.h>

namespace mtl {
namespace internal {

void UniqueHandleTraits::Free(mx_handle_t handle) {
  if (handle > MX_HANDLE_INVALID)
    mx_handle_close(handle);
}

}  // namespace internal
}  // namespace mtl
