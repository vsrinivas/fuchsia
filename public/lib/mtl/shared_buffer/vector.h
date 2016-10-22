// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_SHARED_BUFFER_VECTOR_H_
#define LIB_MTL_SHARED_BUFFER_VECTOR_H_

#include <vector>

#include "mojo/public/cpp/system/buffer.h"

namespace mtl {

// Make a new shared buffer with the contents of a char vector.
bool SharedBufferFromVector(const std::vector<char>& vector,
                            mojo::ScopedSharedBufferHandle* handle_ptr);

// Copy the contents of a shared buffer into a char vector.
bool VectorFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::vector<char>* vector_ptr);

// Make a new shared buffer with the contents of a uint8_t vector.
bool SharedBufferFromVector(const std::vector<uint8_t>& vector,
                            mojo::ScopedSharedBufferHandle* handle_ptr);

// Copy the contents of a shared buffer into a uint8_t vector.
bool VectorFromSharedBuffer(const mojo::ScopedSharedBufferHandle& shared_buffer,
                            std::vector<uint8_t>* vector_ptr);

}  // namespace mtl

#endif  // LIB_MTL_SHARED_BUFFER_VECTOR_H_
