// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTERS_H_
#define LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTERS_H_

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/type_converter.h"

namespace fxl {

// Converts a vector to a FIDL Array with the same element type.
template <typename T>
struct TypeConverter<f1dl::Array<T>, std::vector<T>> {
  static f1dl::Array<T> Convert(const std::vector<T>& value) {
    auto result = f1dl::Array<T>::New(value.size());
    for (size_t i = 0; i < value.size(); ++i)
      result[i] = value[i];
    return result;
  }
};

// Converts a FIDL Array to a vector with the same element type.
template <typename T>
struct TypeConverter<std::vector<T>, f1dl::Array<T>> {
  static std::vector<T> Convert(const f1dl::Array<T>& value) {
    std::vector<T> result;
    if (!value.is_null()) {
      result.resize(value.size());
      for (size_t i = 0; i < value.size(); ++i)
        result[i] = value[i];
    }
    return result;
  }
};

// Converts a FIDL Array to a unique_ptr to a vector with the same element type.
template <typename T>
struct TypeConverter<std::unique_ptr<std::vector<T>>, f1dl::Array<T>> {
  static std::vector<T> Convert(const f1dl::Array<T>& value) {
    if (!value) {
      return nullptr;
    }

    std::vector<T> result;
    if (!value.is_null()) {
      result.resize(value.size());
      for (size_t i = 0; i < value.size(); ++i)
        result[i] = value[i];
    }

    return std::make_unique<std::vector<T>>(std::move(result));
  }
};

// Converts a vector to a FIDL Array with a different element type.
template <typename T, typename U>
struct TypeConverter<f1dl::Array<T>, std::vector<U>> {
  static f1dl::Array<T> Convert(const std::vector<U>& value) {
    auto result = f1dl::Array<T>::New(value.size());
    for (size_t i = 0; i < value.size(); ++i)
      result[i] = To<T>(value[i]);

    return result;
  }
};

// Converts a FIDL Array to a vector with a different element type.
template <typename T, typename U>
struct TypeConverter<std::vector<T>, f1dl::Array<U>> {
  static std::vector<T> Convert(const f1dl::Array<U>& value) {
    std::vector<T> result;
    if (!value.is_null()) {
      result.resize(value.size());
      for (size_t i = 0; i < value.size(); ++i)
        result[i] = To<T>(value[i]);
    }

    return result;
  }
};

// Converts a FIDL Array to a unique_ptr to a vector with a different element
// type.
template <typename T, typename U>
struct TypeConverter<std::unique_ptr<std::vector<T>>, f1dl::Array<U>> {
  static std::unique_ptr<std::vector<T>> Convert(const f1dl::Array<U>& value) {
    if (!value) {
      return nullptr;
    }

    std::vector<T> result;
    if (!value.is_null()) {
      result.resize(value.size());
      for (size_t i = 0; i < value.size(); ++i)
        result[i] = To<T>(value[i]);
    }

    return std::make_unique<std::vector<T>>(std::move(result));
  }
};

// Converts a FIDL String to a std::string.
template <>
struct TypeConverter<std::string, f1dl::String> {
  static std::string Convert(f1dl::String value);
};

// Converts a std::string to a FIDL String.
template <>
struct TypeConverter<f1dl::String, std::string> {
  static f1dl::String Convert(std::string value);
};

}  // namespace fxl

#endif  // LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTERS_H_
