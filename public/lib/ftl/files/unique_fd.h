// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_UNIQUE_FD_H_
#define LIB_FTL_FILES_UNIQUE_FD_H_

#include "lib/ftl/memory/unique_object.h"

namespace ftl {
namespace internal {

struct UniqueFDTraits {
  static int InvalidValue() { return -1; }
  static bool IsValid(int value) { return value >= 0; }
  static void Free(int fd);
};

}  // namespace internal

using UniqueFD = UniqueObject<int, internal::UniqueFDTraits>;

}  // namespace ftl

#endif  // LIB_FTL_FILES_UNIQUE_FD_H_
