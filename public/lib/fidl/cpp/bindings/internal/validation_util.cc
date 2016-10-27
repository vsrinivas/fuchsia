// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/validation_util.h"

#include <limits>
#include <string>

#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"

namespace fidl {
namespace internal {

bool ValidateEncodedPointer(const uint64_t* offset) {
  // - Make sure |*offset| is no more than 32-bits.
  // - Cast |offset| to uintptr_t so overflow behavior is well defined across
  //   32-bit and 64-bit systems.
  return *offset <= std::numeric_limits<uint32_t>::max() &&
         (reinterpret_cast<uintptr_t>(offset) +
              static_cast<uint32_t>(*offset) >=
          reinterpret_cast<uintptr_t>(offset));
}

ValidationError ValidateStructHeaderAndClaimMemory(
    const void* data,
    BoundsChecker* bounds_checker,
    std::string* err) {
  if (!IsAligned(data)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
    return ValidationError::MISALIGNED_OBJECT;
  }
  if (!bounds_checker->IsValidRange(data, sizeof(StructHeader))) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
    return ValidationError::ILLEGAL_MEMORY_RANGE;
  }

  const StructHeader* header = static_cast<const StructHeader*>(data);

  if (header->num_bytes < sizeof(StructHeader)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
    return ValidationError::UNEXPECTED_STRUCT_HEADER;
  }

  if (!bounds_checker->ClaimMemory(data, header->num_bytes)) {
    FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
    return ValidationError::ILLEGAL_MEMORY_RANGE;
  }

  return ValidationError::NONE;
}

}  // namespace internal
}  // namespace fidl
