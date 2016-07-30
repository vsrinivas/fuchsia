// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_UNIQUE_HANDLE_H_
#define LIB_MTL_UNIQUE_HANDLE_H_

#include <magenta/types.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/unique_object.h"

namespace mtl {
namespace internal {

struct UniqueHandleTraits {
  static mx_handle_t InvalidValue() { return MX_HANDLE_INVALID; }
  static bool IsValid(mx_handle_t handle) { return handle > MX_HANDLE_INVALID; }
  static void Free(mx_handle_t handle);
};

}  // namespace internal

using UniqueHandle =
    ftl::UniqueObject<mx_handle_t, internal::UniqueHandleTraits>;

}  // namespace mtl

#endif  // LIB_MTL_UNIQUE_HANDLE_H_
