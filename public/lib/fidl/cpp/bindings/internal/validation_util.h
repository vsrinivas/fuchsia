// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_UTIL_H_

#include <stdint.h>

#include <string>

#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"

namespace fidl {
namespace internal {

// Checks whether decoding the pointer will overflow and produce a pointer
// smaller than |offset|.
bool ValidateEncodedPointer(const uint64_t* offset);

// Validates that |data| contains a valid struct header, in terms of alignment
// and size (i.e., the |num_bytes| field of the header is sufficient for storing
// the header itself). Besides, it checks that the memory range
// [data, data + num_bytes) is not marked as occupied by other objects in
// |bounds_checker|. On success, the memory range is marked as occupied.
// Note: Does not verify |version| or that |num_bytes| is correct for the
// claimed version.
ValidationError ValidateStructHeaderAndClaimMemory(
    const void* data,
    BoundsChecker* bounds_checker,
    std::string* err);

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_VALIDATION_UTIL_H_
