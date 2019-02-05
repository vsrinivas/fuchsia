// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/coding_traits.h"

namespace fidl {

void EncodeNullVector(Encoder* encoder, size_t offset) {
  fidl_vector_t* vector = encoder->GetPtr<fidl_vector_t>(offset);
  vector->count = 0u;
  vector->data = reinterpret_cast<void*>(FIDL_ALLOC_ABSENT);
}

void EncodeVectorPointer(Encoder* encoder, size_t count, size_t offset) {
  fidl_vector_t* vector = encoder->GetPtr<fidl_vector_t>(offset);
  vector->count = count;
  vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
}

}  // namespace fidl
