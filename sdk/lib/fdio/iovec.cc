// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/types.h>

#include "internal.h"

size_t fdio_iovec_get_capacity(const zx_iovec_t* vector, size_t vector_count) {
  size_t total_capacity = 0u;
  for (size_t i = 0; i < vector_count; ++i) {
    total_capacity += vector[i].capacity;
  }
  return total_capacity;
}

void fdio_iovec_copy_to(const uint8_t* buffer, size_t buffer_size, const zx_iovec_t* vector,
                        size_t vector_count, size_t* out_actual) {
  size_t remaining = buffer_size;
  for (size_t i = 0; i < vector_count && remaining > 0; ++i) {
    size_t chunk_size = remaining;
    if (chunk_size > vector[i].capacity) {
      chunk_size = vector[i].capacity;
    }
    memcpy(vector[i].buffer, buffer, chunk_size);
    buffer += chunk_size;
    remaining -= chunk_size;
  }
  *out_actual = buffer_size - remaining;
}

void fdio_iovec_copy_from(const zx_iovec_t* vector, size_t vector_count, uint8_t* buffer,
                          size_t buffer_size, size_t* out_actual) {
  size_t remaining = buffer_size;
  for (size_t i = 0; i < vector_count && remaining > 0; ++i) {
    size_t chunk_size = remaining;
    if (chunk_size > vector[i].capacity) {
      chunk_size = vector[i].capacity;
    }
    memcpy(buffer, vector[i].buffer, chunk_size);
    buffer += chunk_size;
    remaining -= chunk_size;
  }
  *out_actual = buffer_size - remaining;
}
