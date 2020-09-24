// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_COMPARISON_H_
#define LIB_FIDL_CPP_COMPARISON_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "lib/fidl/cpp/types.h"

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

// Comparisons that uses structure equality on on std::unique_ptr instead of
// pointer equality.
namespace fidl {

template <class T>
bool Equals(const T& lhs, const T& rhs);

template <typename T, typename = void>
struct Equality {};

template <class T>
struct Equality<T, typename std::enable_if_t<std::is_integral<T>::value>> {
  constexpr bool operator()(const T& lhs, const T& rhs) const { return lhs == rhs; }
};

template <class T>
struct Equality<T, typename std::enable_if_t<std::is_floating_point<T>::value>> {
  constexpr bool operator()(const T& lhs, const T& rhs) const {
    // TODO(ianloic): do something better for floating point comparison?
    return lhs == rhs;
  }
};

#ifdef __Fuchsia__
template <class T>
struct Equality<T, typename std::enable_if_t<std::is_base_of<zx::object_base, T>::value>> {
  bool operator()(const T& lhs, const T& rhs) const { return lhs.get() == rhs.get(); }
};
#endif  // __Fuchsia__

template <typename T, size_t N>
struct Equality<std::array<T, N>> {
  // N.B.: This may be constexpr-able in C++20.
  bool operator()(const std::array<T, N>& lhs, const std::array<T, N>& rhs) const {
    return std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), ::fidl::Equality<T>{});
  }
};

template <>
struct Equality<std::string> {
  bool operator()(const std::string& lhs, const std::string& rhs) const { return lhs == rhs; }
};

template <class T>
struct Equality<std::vector<T>> {
  bool operator()(const std::vector<T>& lhs, const std::vector<T>& rhs) const {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    return std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(), ::fidl::Equality<T>{});
  }
};

template <class T>
struct Equality<std::unique_ptr<T>> {
  constexpr bool operator()(const std::unique_ptr<T>& lhs, const std::unique_ptr<T>& rhs) const {
    if (lhs == nullptr || rhs == nullptr) {
      return rhs == lhs;
    }
    return ::fidl::Equality<T>{}(*lhs, *rhs);
  }
};

template <>
struct Equality<UnknownBytes> {
  bool operator()(const UnknownBytes& lhs, const UnknownBytes& rhs) const {
    return ::fidl::Equality<std::vector<uint8_t>>{}(lhs.bytes, rhs.bytes);
  }
};

#ifdef __Fuchsia__
template <>
struct Equality<UnknownData> {
  bool operator()(const UnknownData& lhs, const UnknownData& rhs) const {
    return ::fidl::Equality<std::vector<uint8_t>>{}(lhs.bytes, rhs.bytes) &&
           ::fidl::Equality<std::vector<zx::handle>>{}(lhs.handles, rhs.handles);
  }
};
#endif

template <class T>
bool Equals(const T& lhs, const T& rhs) {
  return ::fidl::Equality<T>{}(lhs, rhs);
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_COMPARISON_H_
