// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_VECTOR_H_
#define LIB_FIDL_CPP_VECTOR_H_

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fit/optional.h>
#include <zircon/assert.h>

#include <utility>
#include <vector>

#include "lib/fidl/cpp/traits.h"
#include "lib/fidl/cpp/transition.h"

namespace fidl {

#if defined(FIDL_USE_FIT_OPTIONAL)

template <typename T>
class VectorPtr : public fit::optional<std::vector<T>> {
 public:
  constexpr VectorPtr() = default;

  constexpr VectorPtr(fit::nullopt_t) noexcept {}
  FIDL_FIT_OPTIONAL_DEPRECATED("Use fit::nullopt instead of nullptr")
  constexpr VectorPtr(std::nullptr_t) noexcept {}

  VectorPtr(const VectorPtr&) = default;
  VectorPtr& operator=(const VectorPtr&) = default;

  VectorPtr(VectorPtr&&) noexcept = default;
  VectorPtr& operator=(VectorPtr&&) noexcept = default;

  // Move construct and move assignment from the value type
  constexpr VectorPtr(std::vector<T>&& value) : fit::optional<std::vector<T>>(std::move(value)) {}
  constexpr VectorPtr& operator=(std::vector<T>&& value) {
    fit::optional<std::vector<T>>::operator=(std::move(value));
    return *this;
  }

  // Copy construct and copy assignment from the value type
  constexpr VectorPtr(const std::vector<T>& value) : fit::optional<std::vector<T>>(value) {}
  constexpr VectorPtr& operator=(const std::vector<T>& value) {
    fit::optional<std::vector<T>>::operator=(value);
    return *this;
  }

  explicit VectorPtr(size_t size) : fit::optional<std::vector<T>>(size) {}

  // Override unchecked accessors with versions that check.
  constexpr std::vector<T>* operator->() {
    if (!fit::optional<std::vector<T>>::has_value()) {
      __builtin_trap();
    }
    return fit::optional<std::vector<T>>::operator->();
  }
  constexpr const std::vector<T>* operator->() const {
    if (!fit::optional<std::vector<T>>::has_value()) {
      __builtin_trap();
    }
    return fit::optional<std::vector<T>>::operator->();
  }

  FIDL_FIT_OPTIONAL_DEPRECATED("Assign an empty std::vector")
  VectorPtr& emplace() {
    *this = std::move(std::vector<T>());
    return *this;
  }

  FIDL_FIT_OPTIONAL_DEPRECATED("Assign an std::vector")
  VectorPtr& emplace(std::initializer_list<std::vector<T>>&& ilist) {
    *this = (std::move(std::vector<T>(ilist)));
    return *this;
  }

  FIDL_FIT_OPTIONAL_DEPRECATED("Assign an std::vector")
  VectorPtr& emplace(std::vector<T>&& value) {
    *this = std::move(value);
    return *this;
  }
};

#else

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
  explicit VectorPtr(size_t size) : vec_(std::vector<T>(size)), is_null_if_empty_(false) {}
  VectorPtr(std::vector<T>&& vec) : vec_(std::move(vec)), is_null_if_empty_(false) {}

  VectorPtr(VectorPtr&& other) = default;
  VectorPtr& operator=(VectorPtr&& other) = default;

  // Copy construct and assignment from a const std::vector<T>&
  VectorPtr(const std::vector<T>& other) : vec_(other), is_null_if_empty_(false) {}
  VectorPtr& operator=(const std::vector<T>& other) {
    vec_ = other;
    is_null_if_empty_ = false;
    return *this;
  }

  // Creates a VectorPtr of the given size.
  //
  // Equivalent to using the |VectorPtr(size_t)| constructor.
  FIDL_FIT_OPTIONAL_DEPRECATED("use constructor")
  static VectorPtr New(size_t size) { return VectorPtr(size); }

  // Accesses the underlying std::vector object.
  FIDL_FIT_OPTIONAL_DEPRECATED("use value_or")
  const std::vector<T>& get() const { return vec_; }

  // Takes the std::vector from the VectorPtr.
  //
  // After this method returns, the VectorPtr is null.
  FIDL_FIT_OPTIONAL_DEPRECATED("use std::move(vecptr).value();vecptr.reset();")
  std::vector<T> take() {
    is_null_if_empty_ = true;
    return std::move(vec_);
  }

  // Stores the given std::vector in this VectorPtr.
  //
  // After this method returns, the VectorPtr is non-null.
  FIDL_FIT_OPTIONAL_DEPRECATED("use assignment or emplace()")
  void reset(std::vector<T> vec) {
    vec_ = std::move(vec);
    is_null_if_empty_ = false;
  }

  VectorPtr& emplace() {
    vec_.clear();
    is_null_if_empty_ = false;
    return *this;
  }

  VectorPtr& emplace(std::vector<T>&& vec) {
    vec_ = std::move(vec);
    is_null_if_empty_ = false;
    return *this;
  }

  VectorPtr& emplace(std::initializer_list<T>&& ilist) {
    vec_ = std::vector<T>(ilist);
    is_null_if_empty_ = false;
    return *this;
  }

  VectorPtr& operator=(std::vector<T>&& vec) {
    vec_ = std::move(vec);
    is_null_if_empty_ = false;
    return *this;
  }

  void reset() {
    vec_.clear();
    is_null_if_empty_ = true;
  }

  // Resizes the underlying std::vector in this VectorPtr to the given size.
  //
  // After this method returns, the VectorPtr is non-null.
  FIDL_FIT_OPTIONAL_DEPRECATED("initialize and use operator->")
  void resize(size_t size) {
    vec_.resize(size);
    is_null_if_empty_ = false;
  }

  // Pushes |value| onto the back of this VectorPtr.
  //
  // If this vector was null, it will become non-null with a size of 1.
  FIDL_FIT_OPTIONAL_DEPRECATED("initialize and use operator->")
  void push_back(const T& value) {
    vec_.push_back(value);
    is_null_if_empty_ = false;
  }

  // Pushes |value| onto the back of this VectorPtr.
  //
  // If this vector was null, it will become non-null with a size of 1.
  FIDL_FIT_OPTIONAL_DEPRECATED("initialize and use operator->")
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
  FIDL_FIT_OPTIONAL_DEPRECATED("use fidl::Clone()")
  VectorPtr Clone() const {
    if (is_null())
      return VectorPtr();
    return VectorPtr(vec_);
  }

  // Whether this VectorPtr is null.
  //
  // The null state is separate from the empty state.
  FIDL_FIT_OPTIONAL_DEPRECATED("use !has_value()")
  bool is_null() const { return is_null_if_empty_ && vec_.empty(); }

  bool has_value() const { return !is_null_if_empty_ || !vec_.empty(); }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return has_value(); }

  // Provides access to the underlying std::vector.
  std::vector<T>* operator->() { return &vec_; }
  const std::vector<T>* operator->() const { return &vec_; }

  // Provides access to the underlying std::vector.
  std::vector<T>& operator*() { return vec_; }
  const std::vector<T>& operator*() const { return vec_; }

  std::vector<T>& value() & { return vec_; }
  const std::vector<T>& value() const& { return vec_; }

  std::vector<T>& value_or(std::vector<T>& default_value) & {
    return has_value() ? vec_ : default_value;
  }
  const std::vector<T>& value_or(const std::vector<T>& default_value) const& {
    return has_value() ? vec_ : default_value;
  }

  // Provides implicit conversion for accessing the underlying std::vector.
  // To mutate the vector, use operator* or operator-> or one of the mutation
  // functions.
  FIDL_FIT_OPTIONAL_DEPRECATED("use value_or()")
  operator const std::vector<T> &() const { return vec_; }

 private:
  std::vector<T> vec_;
  bool is_null_if_empty_;
};

#endif

template <class T>
struct Equality<VectorPtr<T>> {
  bool operator()(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) const {
    if (!lhs.has_value() || !rhs.has_value()) {
      return !lhs.has_value() == !rhs.has_value();
    }
    return ::fidl::Equality<std::vector<T>>{}(lhs.value(), rhs.value());
  }

  // TODO(46638): Remove this when all clients have been transitioned to functor.
  static inline bool Equals(const VectorPtr<T>& lhs, const VectorPtr<T>& rhs) {
    return ::fidl::Equality<VectorPtr<T>>{}(lhs, rhs);
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_VECTOR_H_
