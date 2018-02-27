// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_VECTOR_H_
#define LIB_FIDL_CPP_VECTOR_H_

#include <fidl/cpp/builder.h>
#include <fidl/cpp/vector_view.h>

#include <utility>
#include <vector>

#include "lib/fidl/cpp/traits.h"

namespace fidl {

// A representation of a FIDL vector that owns the memory for the vector.
//
// A VectorPtr has three states: (1) null, (2) empty, (3) contains data. In
// the first and second states, operations that return an std::vector return
// the empty std::vector. The null and empty states can be distinguished using
// the |is_null| and |operator bool| methods.
template <typename T>
class VectorPtr {
 public:
  VectorPtr() : is_null_(true) {}
  explicit VectorPtr(std::vector<T> vec)
      : vec_(std::move(vec)), is_null_(false) {}

  VectorPtr(const VectorPtr&) = delete;
  VectorPtr& operator=(const VectorPtr&) = delete;

  VectorPtr(VectorPtr&& other)
      : vec_(std::move(other.vec_)), is_null_(other.is_null_) {}

  VectorPtr& operator=(VectorPtr&& other) {
    vec_ = std::move(other.vec_);
    is_null_ = other.is_null_;
    return *this;
  }

  // Accesses the underlying std::vector object.
  //
  // The returned vector will be empty if the VectorPtr is either null or empty.
  std::vector<T>& get() { return vec_; }
  const std::vector<T>& get() const { return vec_; }

  // Stores the given std::vector in this VectorPtr.
  //
  // After this method returns, the VectorPtr is non-null.
  void reset(std::vector<T> vec) {
    vec_ = vec;
    is_null_ = false;
  }

  void swap(VectorPtr& other) {
    using std::swap;
    swap(vec_, other.vec_);
    swap(is_null_, other.is_null_);
  }

  // Whether this VectorPtr is null.
  //
  // The null state is separate from the empty state.
  bool is_null() const { return is_null_; }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  // Provides access to the underlying std::vector.
  std::vector<T>* operator->() { return &vec_; }
  const std::vector<T>* operator->() const { return &vec_; }

  // Provides access to the underlying std::vector.
  std::vector<T>& operator*() { return vec_; }
  const std::vector<T>& operator*() const { return vec_; }

 private:
  std::vector<T> vec_;
  bool is_null_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_VECTOR_H_
