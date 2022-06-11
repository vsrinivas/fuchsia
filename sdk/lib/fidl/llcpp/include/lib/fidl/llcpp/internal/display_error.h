// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_DISPLAY_ERROR_H_
#define LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_DISPLAY_ERROR_H_

#include <zircon/fidl.h>

#include <cstddef>
#include <cstdint>

namespace fidl::internal {

// |DisplayError| should be explicitly specialized for error-like types which
// could be printed: transport errors, application errors, etc.
template <typename T>
struct DisplayError {
  // Formats the description into a buffer |destination| that is of size
  // |capacity|. The description will cut off at `capacity - 1`. It inserts a
  // trailing NUL.
  //
  // Returns how many bytes were written, not counting the NUL.
  //
  // Explicit specializations should define a method with this signature.
  // This particular method does not have an implementation, such that missed
  // specializations would be detected by a link-time error.
  static size_t Format(const T& value, char* destination, size_t capacity);
};

// Convenience wrapper for |DisplayError<T>::Format|.
template <typename T>
size_t FormatDisplayError(const T& value, char* destination, size_t capacity) {
  return DisplayError<T>::Format(value, destination, capacity);
}

// Built-in support for printing raw numerical errors.
// TODO(fxbug.dev/95209): |zx_status_t| dispatches to this path today.
// Ideally we would like to print the human readable status name.
template <>
struct fidl::internal::DisplayError<int32_t> {
  static size_t Format(const int32_t& value, char* destination, size_t capacity);
};
template <>
struct fidl::internal::DisplayError<uint32_t> {
  static size_t Format(const uint32_t& value, char* destination, size_t capacity);
};

// FormatApplicationError| helps to format application errors represented using
// enums. It implements the common part of formatting and string concatenation.
//
// |member_name| is a string literal that is the C++ member name corresponding
// to |value|, which must be a FIDL 32-bit enum. Signed values should go through
// casting.
//
// If the |value| is not a known enum member, |member_name| should be |nullptr|.
size_t FormatApplicationError(uint32_t value, const char* member_name, const fidl_type_t* type,
                              char* destination, size_t capacity);

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INCLUDE_LIB_FIDL_LLCPP_INTERNAL_DISPLAY_ERROR_H_
