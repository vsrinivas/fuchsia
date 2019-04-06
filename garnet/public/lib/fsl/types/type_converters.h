// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_TYPES_TYPE_CONVERTERS_H_
#define LIB_FSL_TYPES_TYPE_CONVERTERS_H_

#include <array>

#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/type_converter.h"
#include "lib/fidl/cpp/vector.h"

namespace fidl {

// Default conversion from a type to itself, to simplify container converters.
template <typename T>
struct TypeConverter<T, T> {
  static T Convert(const T& value) { return value; }
};

// Converts a vector to a FIDL VectorPtr.
template <typename T, typename U>
struct TypeConverter<fidl::VectorPtr<T>, std::vector<U>> {
  static fidl::VectorPtr<T> Convert(const std::vector<U>& value) {
    fidl::VectorPtr<T> result(value.size());
    for (size_t i = 0; i < value.size(); ++i)
      result->at(i) = To<T>(value[i]);
    return result;
  }
};

// Converts a FIDL VectorPtr to a vector.
template <typename T, typename U>
struct TypeConverter<std::vector<T>, fidl::VectorPtr<U>> {
  static std::vector<T> Convert(const fidl::VectorPtr<U>& value) {
    std::vector<T> result;
    if (!value.is_null()) {
      result.resize(value->size());
      for (size_t i = 0; i < value->size(); ++i) {
        result[i] = To<T>((*value)[i]);
      }
    }
    return result;
  }
};

// Converts a FIDL Array to a FIDL VectorPtr.
template <typename T, typename U, size_t N>
struct TypeConverter<fidl::VectorPtr<T>, std::array<U, N>> {
  static fidl::VectorPtr<T> Convert(const std::array<U, N>& value) {
    fidl::VectorPtr<T> result(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
      result->at(i) = To<T>(value[i]);
    }
    return result;
  }
};

// Converts a FIDL Array to a vector.
template <typename T, typename U, size_t N>
struct TypeConverter<std::vector<T>, std::array<U, N>> {
  static std::vector<T> Convert(const std::array<U, N>& value) {
    std::vector<T> result(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
      result[i] = To<T>(value[i]);
    }
    return result;
  }
};

// Converts a FIDL String into a string.
template <>
struct TypeConverter<std::string, fidl::StringPtr> {
  static std::string Convert(const fidl::StringPtr& value);
};

// Converts a string to a FIDL String.
template <>
struct TypeConverter<fidl::StringPtr, std::string> {
  static fidl::StringPtr Convert(const std::string& value);
};

}  // namespace fidl

#endif  // LIB_FSL_TYPES_TYPE_CONVERTERS_H_
