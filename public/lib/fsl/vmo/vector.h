// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_VMO_VECTOR_H_
#define LIB_FSL_VMO_VECTOR_H_

#include <lib/zx/vmo.h>

#include <vector>

#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/fxl_export.h"

namespace fsl {

// Make a new shared buffer with the contents of a char vector.
FXL_EXPORT bool VmoFromVector(const std::vector<char>& vector,
                              SizedVmo* sized_vmo_ptr);

// Copy the contents of a shared buffer into a char vector.
FXL_EXPORT bool VectorFromVmo(const SizedVmo& shared_buffer,
                              std::vector<char>* vector_ptr);

// Copy the contents of a shared buffer into a char vector.
FXL_EXPORT bool VectorFromVmo(const fuchsia::mem::Buffer& shared_buffer,
                              std::vector<char>* vector_ptr);

// Make a new shared buffer with the contents of a uint8_t vector.
FXL_EXPORT bool VmoFromVector(const std::vector<uint8_t>& vector,
                              SizedVmo* sized_vmo_ptr);

// Copy the contents of a shared buffer into a uint8_t vector.
FXL_EXPORT bool VectorFromVmo(const SizedVmo& shared_buffer,
                              std::vector<uint8_t>* vector_ptr);

// Copy the contents of a shared buffer into a uint8_t vector.
FXL_EXPORT bool VectorFromVmo(const fuchsia::mem::Buffer& shared_buffer,
                              std::vector<uint8_t>* vector_ptr);

}  // namespace fsl

#endif  // LIB_FSL_VMO_VECTOR_H_
