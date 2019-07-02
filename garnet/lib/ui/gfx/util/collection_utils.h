// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_COLLECTION_UTILS_H_
#define GARNET_LIB_UI_GFX_UTIL_COLLECTION_UTILS_H_

#include <utility>
#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

// Iterate over the weak ptrs in |vect_in|.  If they are non-null, apply the
// closure to the pointer.  Otherwise, erase that entry from the vector (this
// is done efficiently, resizing the vector only once at the end).
template <typename T, typename ClosureT>
size_t ApplyToCompactedVector(std::vector<fxl::WeakPtr<T>>* vect_in, ClosureT closure) {
  FXL_DCHECK(vect_in);
  auto& vect = *vect_in;
  const size_t initial_size = vect.size();
  size_t size = initial_size;
  size_t i = 0;
  while (i < size) {
    if (vect[i]) {
      closure(vect[i].get());
      ++i;
    } else {
      std::swap(vect[i], vect[size - 1]);
      --size;
    }
  }
  size_t num_compacted = initial_size - size;
  if (num_compacted) {
    vect.resize(size);
  }
  return num_compacted;
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_UTIL_COLLECTION_UTILS_H_
