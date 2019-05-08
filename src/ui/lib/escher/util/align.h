// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_ALIGN_H_
#define SRC_UI_LIB_ESCHER_UTIL_ALIGN_H_

#include <cstdint>
#include <memory>

#include "src/lib/fxl/logging.h"

namespace escher {

// If |position| is already aligned to |alignment|, return it.  Otherwise,
// return the next-larger value that is so aligned.  |alignment| must be
// positive; the result is undefined otherwise.
inline size_t AlignedToNext(size_t position, size_t alignment) {
  FXL_DCHECK(alignment);
  size_t remainder = position % alignment;
  return remainder ? position + (alignment - remainder) : position;
}

inline uint8_t* AlignedToNext(uint8_t* ptr, size_t alignment) {
  return reinterpret_cast<uint8_t*>(
      AlignedToNext(reinterpret_cast<size_t>(ptr), alignment));
}

template <typename T>
T* NextAlignedPtr(void* ptr) {
  size_t space = sizeof(T) + alignof(T);
  return reinterpret_cast<T*>(std::align(alignof(T), sizeof(T), ptr, space));
}

template <typename T>
T* NextAlignedTriviallyDestructiblePtr(void* ptr) {
  static_assert(std::is_trivially_destructible<T>::value,
                "Type must be trivially destructible.");
  return NextAlignedPtr<T>(ptr);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_ALIGN_H_
