// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_VECTOR_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_VECTOR_H_

#include <zircon/types.h>

template <typename F>
zx_status_t zxio_do_vector(const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                           F fn) {
  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    size_t actual;
    zx_status_t status = fn(vector[i].buffer, vector[i].capacity, &actual);
    if (status != ZX_OK) {
      // This can't be `i > 0` because the first buffer supplied by the caller
      // might've been of length zero, and we may never have attempted an I/O
      // operation with it.
      if (total > 0) {
        break;
      }
      return status;
    }
    total += actual;
    if (actual != vector[i].capacity) {
      // Short.
      break;
    }
  }
  *out_actual = total;
  return ZX_OK;
}

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_VECTOR_H_
