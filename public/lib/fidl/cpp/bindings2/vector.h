// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_VECTOR_H_
#define LIB_FIDL_CPP_BINDINGS2_VECTOR_H_

#include <fidl/builder.h>
#include <fidl/vector_view.h>

#include <utility>
#include <vector>

#include "lib/fidl/cpp/bindings2/put.h"
#include "lib/fidl/cpp/bindings2/traits.h"

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
  // A representation of a FIDL vector that does not own the memory for the
  // vector. This representation matches the wire format representation of the
  // vector.
  using View = VectorView<typename ViewOf<T>::type>;

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

  // Copies the data from the |VectorView| into a new |VectorPtr| object, which
  // is returned.
  VectorPtr<T> Take(View* view) {
    if (view->is_null())
      return VectorPtr<T>();
    std::vector<T> vec;
    vec.resize(view->count());
    for (size_t i = 0; i < view->count(); ++i)
      Take(&vec.at(i), &view->at(i));
    return VectorPtr<T>(std::move(vec));
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

// Copies the vector data from |vector| into |*view|.
//
// Uses |builder| to allocate storage for the vector data. |*view| must be null
// (e.g., freshly allocated) before calling this function.
//
// Returns whether |vector| was sucessfully copied into |*view|. For example,
// this function could return false if it is unable to allocate sufficient
// storage for the vector data from |builder|.
template<typename T>
bool PutAt(Builder* builder, typename VectorPtr<T>::View* view,
           VectorPtr<T>* vector) {
  using TypeView = typename ViewOf<T>::type;

  if (vector->is_null())
    return true;
  size_t count = (*vector)->size();
  TypeView* data = builder->NewArray<TypeView>(count);
  if (!data)
    return false;
  view->set_data(data);
  view->set_count(count);
  for (size_t i = 0; i < count; ++i)
    PutAt(builder, data + i, &(*vector)->at(i));
  return true;
}

// Creates a VectorView and copies the given vector data into the VectorView.
//
// Uses |builder| to allocate storage for the |VectorView| and the vector data.
//
// Returns the VectorView if successful. Otherwise, returns nullptr. If this
// function succeeds in allocating the VectorView but fails to allocate the
// vector data, the function returns nullptr and does not roll back the
// VectorView allocation in |builder|.
template<typename T>
typename ViewOf<T>::type Build(Builder* builder, const VectorPtr<T>& vector) {
  using TypeView = typename ViewOf<T>::type;

  TypeView* view = builder->New<TypeView>();
  if (view && PutAt(builder, view, vector))
    return view;
  return nullptr;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_VECTOR_H_
