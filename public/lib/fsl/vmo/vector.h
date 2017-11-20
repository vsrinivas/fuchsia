// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_VMO_VECTOR_H_
#define LIB_FSL_VMO_VECTOR_H_

#include <zx/vmo.h>

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
FXL_EXPORT bool VectorFromVmo(const SizedVmoTransportPtr& shared_buffer,
                              std::vector<char>* vector_ptr);

// Make a new shared buffer with the contents of a uint8_t vector.
FXL_EXPORT bool VmoFromVector(const std::vector<uint8_t>& vector,
                              SizedVmo* sized_vmo_ptr);

// Copy the contents of a shared buffer into a uint8_t vector.
FXL_EXPORT bool VectorFromVmo(const SizedVmo& shared_buffer,
                              std::vector<uint8_t>* vector_ptr);

// Copy the contents of a shared buffer into a uint8_t vector.
FXL_EXPORT bool VectorFromVmo(const SizedVmoTransportPtr& shared_buffer,
                              std::vector<uint8_t>* vector_ptr);

// Make a new shared buffer with the contents of a char vector.
//
// This function is deprecated because it loses the vector size when the vmo is
// always page aligned.
FXL_EXPORT bool VmoFromVector(const std::vector<char>& vector,
                              zx::vmo* handle_ptr)
    __attribute__((__deprecated__("Use the version with a SizedVmo.")));

// Copy the contents of a shared buffer into a char vector.
//
// This function is deprecated because it loses the vector size when the vmo is
// always page aligned.
FXL_EXPORT bool VectorFromVmo(const zx::vmo& shared_buffer,
                              std::vector<char>* vector_ptr)
    __attribute__((__deprecated__("Use the version with a SizedVmo.")));

// Make a new shared buffer with the contents of a uint8_t vector.
//
// This function is deprecated because it loses the vector size when the vmo is
// always page aligned.
FXL_EXPORT bool VmoFromVector(const std::vector<uint8_t>& vector,
                              zx::vmo* handle_ptr)
    __attribute__((__deprecated__("Use the version with a SizedVmo.")));

// Copy the contents of a shared buffer into a uint8_t vector.
//
// This function is deprecated because it loses the vector size when the vmo is
// always page aligned.
FXL_EXPORT bool VectorFromVmo(const zx::vmo& shared_buffer,
                              std::vector<uint8_t>* vector_ptr)
    __attribute__((__deprecated__("Use the version with a SizedVmo.")));

}  // namespace fsl

#endif  // LIB_FSL_VMO_VECTOR_H_
