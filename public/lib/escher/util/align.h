// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/logging.h"

namespace escher {

// If |position| is already aligned to |alignment|, return it.  Otherwise,
// return the next-larger value that is so aligned.  If |alignment| is zero,
// |position| is always considered to be aligned.
inline size_t AlignedToNext(size_t position, size_t alignment) {
  if (alignment && position % alignment) {
    size_t result = position + (alignment - position % alignment);
    // TODO: remove DCHECK and add unit test.
    FXL_DCHECK(result % alignment == 0);
    return result;
  }
  return position;
}

}  // namespace escher