// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ARRAY_H_
#define LIB_FIDL_CPP_ARRAY_H_

#include <stddef.h>
#include <string.h>

#include <lib/fidl/cpp/comparison.h>

namespace fidl {

template <typename T, size_t N>
class Array {
 public:
  Array() { memset(data_, 0, sizeof(data_)); }

  constexpr size_t size() const { return N; }

  // TODO(FIDL-245) Remove this overload.
  constexpr size_t count() const { return N; }

  const T* data() const { return data_; }
  T* data() { return data_; }

  // TODO(FIDL-245) Remove this overload.
  T* mutable_data() { return data_; }

  const T& at(size_t offset) const { return data()[offset]; }
  T& at(size_t offset) { return mutable_data()[offset]; }

  const T& operator[](size_t offset) const { return at(offset); }
  T& operator[](size_t offset) { return at(offset); }

  T* begin() { return mutable_data(); }
  const T* begin() const { return data(); }
  const T* cbegin() const { return data(); }

  T* end() { return mutable_data() + count(); }
  const T* end() const { return data() + count(); }
  const T* cend() const { return data() + count(); }

 private:
  static_assert(N > 0, "fid::Array cannot have zero elements.");

  T data_[N];
};

template <typename T, size_t N>
bool operator==(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  for (size_t i = 0; i < N; ++i) {
    if (!Equals(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

template <typename T, size_t N>
bool operator!=(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  return !(lhs == rhs);
}

template <typename T, size_t N>
bool operator<(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  for (size_t i = 0; i < N; i++) {
    if (lhs[i] != rhs[i]) {
      return lhs[i] < rhs[i];
    }
  }
  return false;
}

template <typename T, size_t N>
bool operator>(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  for (size_t i = 0; i < N; i++) {
    if (lhs[i] != rhs[i]) {
      return lhs[i] > rhs[i];
    }
  }
  return false;
}

template <typename T, size_t N>
bool operator<=(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  return !(lhs > rhs);
}

template <typename T, size_t N>
bool operator>=(const Array<T, N>& lhs, const Array<T, N>& rhs) {
  return !(lhs < rhs);
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ARRAY_H_
