// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_
#define GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_

#include <memory>
#include <vector>

// Comparisons that uses structure equality on on std::unique_ptr instead of
// pointer equality.
namespace fidl {

template <class T>
inline bool Equals(const T& lhs, const T& rhs) {
  return lhs == rhs;
}

template <class T>
inline bool Equals(const std::unique_ptr<T>& lhs,
                   const std::unique_ptr<T>& rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return rhs == lhs;
  }
  return Equals<T>(*lhs, *rhs);
}

template <class T>
inline bool Equals(const std::vector<std::unique_ptr<T>>& lhs,
                   const std::vector<std::unique_ptr<T>>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); i++) {
    const std::unique_ptr<T>& lptr = lhs.at(i);
    const std::unique_ptr<T>& rptr = rhs.at(i);
    if (!Equals<T>(lptr, rptr)) {
      return false;
    }
  }
  return true;
}

}  // namespace fidl

#endif  // GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_
