// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_VECTOR_H_
#define LIB_FIDL_CPP_VECTOR_H_

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fidl/cpp/vector_view.h>

#include <utility>
#include <vector>

#include <zircon/assert.h>

#include "lib/fidl/cpp/traits.h"

namespace fidl {

// A representation of a FIDL vector that owns the memory for the vector.
//
// A VectorPtr has three states: (1) null, (2) empty, (3) contains data.  You
// can check for the null state using the |is_null| method.
template <typename T>
class VectorPtr {
 public:
  VectorPtr() : is_null_if_empty_(true) {}
  ~VectorPtr() = default;
  VectorPtr(std::nullptr_t) : is_null_if_empty_(true) {}
  explicit VectorPtr(size_t size)
      : vec_(std::vector<T>(size)), is_null_if_empty_(false) {}
  explicit VectorPtr(std::vector<T> vec)
      : vec_(std::move(vec)), is_null_if_empty_(false) {}

  VectorPtr(const VectorPtr&) = delete;
  VectorPtr& operator=(const VectorPtr&) = delete;

  VectorPtr(VectorPtr&& other) = default;
  VectorPtr& operator=(VectorPtr&& other) = default;

  // Creates a VectorPtr of the given size.
  //
  // Equivalent to using the |VectorPtr(size_t)| constructor.
  static VectorPtr New(size_t size) { return VectorPtr(size); }

  // Accesses the underlying std::vector object.
  const std::vector<T>& get() const { return vec_; }

  // Takes the std::vector from the VectorPtr.
  //
  // After this method returns, the VectorPtr is null.
  std::vector<T> take() {
    is_null_if_empty_ = true;
    return std::move(vec_);
  }

  // Stores the given std::vector in this VectorPtr.
  //
  // After this method returns, the VectorPtr is non-null.
  void reset(std::vector<T> vec) {
    vec_ = std::move(vec);
    is_null_if_empty_ = false;
  }

  void reset() {
    vec_.clear();
    is_null_if_empty_ = true;
  }

  // Resizes the underlying std::vector in this VectorPtr to the given size.
  //
  // After this method returns, the VectorPtr is non-null.
  void resize(size_t size) {
    vec_.resize(size);
    is_null_if_empty_ = false;
  }

  // Pushes |value| onto the back of this VectorPtr.
  //
  // If this vector was null, it will become non-null with a size of 1.
  void push_back(const T& value) {
    vec_.push_back(value);
    is_null_if_empty_ = false;
  }

  // Pushes |value| onto the back of this VectorPtr.
  //
  // If this vector was null, it will become non-null with a size of 1.
  void push_back(T&& value) {
    vec_.push_back(std::forward<T>(value));
    is_null_if_empty_ = false;
  }

  void swap(VectorPtr& other) {
    using std::swap;
    swap(vec_, other.vec_);
    swap(is_null_if_empty_, other.is_null_if_empty_);
  }

  // Returns a copy of this VectorPtr.
  //
  // Unlike fidl::Clone, this function can never fail. However, this function
  // works only if T is copiable.
  VectorPtr Clone() const {
    if (is_null())
      return VectorPtr();
    return VectorPtr(vec_);
  }

  // Whether this VectorPtr is null.
  //
  // The null state is separate from the empty state.
  bool is_null() const { return is_null_if_empty_ && vec_.empty(); }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null(); }

  // Provides access to the underlying std::vector.
  std::vector<T>* operator->() { return &vec_; }
  const std::vector<T>* operator->() const { return &vec_; }

  // Provides access to the underlying std::vector.
  std::vector<T>& operator*() { return vec_; }
  const std::vector<T>& operator*() const { return vec_; }

  operator const std::vector<T>&() const { return vec_; }

 private:
  std::vector<T> vec_;
  bool is_null_if_empty_;
};

template <class T>
inline bool operator==(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  if (lhs.is_null() || rhs.is_null()) {
    return lhs.is_null() == rhs.is_null();
  }
  if (lhs->size() != rhs->size()) {
    return false;
  }
  for (size_t i = 0; i < lhs->size(); ++i) {
    if (!Equals(lhs->at(i), rhs->at(i))) {
      return false;
    }
  }
  return true;
}

template <class T>
inline bool operator!=(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  return !(lhs == rhs);
}

template <class T>
inline bool operator<(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  if (lhs.is_null() || rhs.is_null()) {
    return !rhs.is_null();
  }
  return *lhs < *rhs;
}

template <class T>
inline bool operator>(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  if (lhs.is_null() || rhs.is_null()) {
    return !lhs.is_null();
  }
  return *lhs > *rhs;
}

template <class T>
inline bool operator<=(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  return !(lhs > rhs);
}

template <class T>
inline bool operator>=(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
  return !(lhs < rhs);
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_VECTOR_H_
