// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIP_UNIQUE_ZIPPER_H_
#define LIB_ZIP_UNIQUE_ZIPPER_H_

#include "lib/fxl/memory/unique_object.h"

namespace zip {

struct UniqueZipperTraits {
  static inline void* InvalidValue() { return nullptr; }
  static inline bool IsValid(void* value) { return value != InvalidValue(); }
  static void Free(void* file);
};

using UniqueZipper = fxl::UniqueObject<void*, UniqueZipperTraits>;

}  // namespace zip

#endif  // LIB_ZIP_UNIQUE_ZIPPER_H_
