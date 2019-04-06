// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_COMPARISON_H_
#define LIB_FIDL_CPP_COMPARISON_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

// Comparisons that uses structure equality on on std::unique_ptr instead of
// pointer equality.
namespace fidl {

template <typename T, typename = void>
struct Equality {};

template <class T>
struct Equality<T, typename std::enable_if_t<std::is_integral<T>::value>> {
  static inline bool Equals(const T& lhs, const T& rhs) {
    return lhs == rhs;
  }
};

template <class T>
struct Equality<T, typename std::enable_if_t<std::is_floating_point<T>::value>> {
  static inline bool Equals(const T& lhs, const T& rhs) {
    // TODO(ianloic): do something better for floating point comparison?
    return lhs == rhs;
  }
};

#ifdef __Fuchsia__
template <class T>
struct Equality<T, typename std::enable_if_t<std::is_base_of<zx::object_base, T>::value>> {
  static inline bool Equals(const T& lhs, const T& rhs) {
    return lhs.get() == rhs.get();
  }
};
#endif  // __Fuchsia__

template <typename T, size_t N>
struct Equality<std::array<T, N>> {
  static inline bool Equals(const std::array<T, N>& lhs, const std::array<T, N>& rhs) {
    for (size_t i = 0; i < N; ++i) {
      if (!Equality<T>::Equals(lhs[i], rhs[i])) {
        return false;
      }
    }
    return true;
  }
};

template <>
struct Equality<std::string> {
  static inline bool Equals(const std::string& lhs, const std::string& rhs) {
    return lhs == rhs;
  }
};

template <class T>
struct Equality<std::vector<T>> {
  static inline bool Equals(const std::vector<T>& lhs,
                           const std::vector<T>& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (size_t i = 0; i < lhs.size(); i++) {
      if (!::fidl::Equality<T>::Equals(lhs[i], rhs[i])) {
        return false;
      }
    }
    return true;
  }
};

template <class T>
struct Equality<std::unique_ptr<T>> {
  static inline bool Equals(const std::unique_ptr<T>& lhs,
                     const std::unique_ptr<T>& rhs) {
    if (lhs == nullptr || rhs == nullptr) {
      return rhs == lhs;
    }
    return ::fidl::Equality<T>::Equals(*lhs, *rhs);
  }
};

template <class T>
inline bool Equals(const T& lhs, const T& rhs) {
  return Equality<T>::Equals(lhs, rhs);
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_COMPARISON_H_
