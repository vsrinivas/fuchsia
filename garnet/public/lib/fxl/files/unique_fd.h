// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_UNIQUE_FD_H_
#define LIB_FXL_FILES_UNIQUE_FD_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/memory/unique_object.h"

namespace fxl {
namespace internal {

struct UniqueFDTraits {
  static int InvalidValue() { return -1; }
  static bool IsValid(int value) { return value >= 0; }
  FXL_EXPORT static void Free(int fd);
};

}  // namespace internal

using UniqueFD = UniqueObject<int, internal::UniqueFDTraits>;

}  // namespace fxl

#endif  // LIB_FXL_FILES_UNIQUE_FD_H_
