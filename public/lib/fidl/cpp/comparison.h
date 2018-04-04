// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_
#define GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_

#include <memory>

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

}  // namespace fidl

#endif  // GARNET_PUBLIC_LIB_FIDL_CPP_COMPARISON_H_
