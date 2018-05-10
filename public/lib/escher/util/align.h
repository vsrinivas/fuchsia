// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_ALIGN_H_
#define LIB_ESCHER_UTIL_ALIGN_H_

#include <memory>

#include "lib/fxl/logging.h"

namespace escher {

// If |position| is already aligned to |alignment|, return it.  Otherwise,
// return the next-larger value that is so aligned.  |alignment| must be
// positive; the result is undefined otherwise.
inline size_t AlignedToNext(size_t position, size_t alignment) {
  FXL_DCHECK(alignment);
  size_t remainder = position % alignment;
  return remainder ? position + (alignment - remainder) : position;
}

template <typename T>
T* NextAlignedPtr(void* ptr) {
  size_t space = sizeof(T) + alignof(T);
  return reinterpret_cast<T*>(std::align(alignof(T), sizeof(T), ptr, space));
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_ALIGN_H_
