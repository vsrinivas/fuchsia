// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_ARRAY_H_
#define LIB_FIDL_CPP_BINDINGS_ARRAY_H_

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/internal/template_util.h"
#include "lib/fidl/cpp/bindings/macros.h"
#include "lib/fidl/cpp/bindings/type_converter.h"

namespace f1dl {

// Represents a moveable array with contents of type |T|. The array can be null,
// meaning that no value has been assigned to it. Null is distinct from empty.
template <typename T>
class Array {
 public:
  //////////////////////////////////////////////////////////////////////////////
  // FIDL2 interface
  //////////////////////////////////////////////////////////////////////////////

  Array() : is_null_(true) {}
  Array(std::nullptr_t) : is_null_(true) {}
  explicit Array(size_t size) : vec_(std::vector<T>(size)), is_null_(false) {}
  explicit Array(std::vector<T> vec) : vec_(std::move(vec)), is_null_(false) {}

  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;

  Array(Array&& other)
      : vec_(std::move(other.vec_)), is_null_(other.is_null_) {}

  Array& operator=(Array&& other) {
    vec_ = std::move(other.vec_);
    is_null_ = other.is_null_;
    return *this;
  }

  // Accesses the underlying std::vector object.
  const std::vector<T>& get() const { return vec_; }

  // Takes the std::vector from the VectorPtr.
  //
  // After this method returns, the VectorPtr is null.
  std::vector<T> take() {
    is_null_ = true;
    return std::move(vec_);
  }

  // Stores the given std::vector in this VectorPtr.
  //
  // After this method returns, the VectorPtr is non-null.
  void reset(std::vector<T> vec) {
    vec_ = vec;
    is_null_ = false;
  }

  void reset() {
    vec_.clear();
    is_null_ = true;
  }

  // Resizes the underlying std::vector in this VectorPtr to the given size.
  //
  // After this method returns, the VectorPtr is non-null.
  void resize(size_t size) {
    vec_.resize(size);
    is_null_ = false;
  }

  // Pushes |value| onto the back of the array. If this array was null, it
  // will become non-null with a size of 1.
  void push_back(const T& value) {
    is_null_ = false;
    vec_.push_back(value);
  }

  void push_back(T&& value) {
    is_null_ = false;
    vec_.push_back(std::forward<T>(value));
  }

  void swap(Array& other) {
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
  const std::vector<T>& operator*() const { return vec_; }

  operator const std::vector<T>&() const { return vec_; }

  //////////////////////////////////////////////////////////////////////////////
  // FIDL1 interface
  //////////////////////////////////////////////////////////////////////////////

  using ConstRefType = typename std::vector<T>::const_reference;
  using RefType = typename std::vector<T>::reference;
  using ConstIterator = typename std::vector<T>::const_iterator;
  using Iterator = typename std::vector<T>::iterator;

  using Traits = internal::ArrayTraits<T, internal::IsMoveOnlyType<T>::value>;
  using ForwardType = typename Traits::ForwardType;

  typedef internal::Array_Data<typename internal::WrapperTraits<T>::DataType>
      Data_;

  // Returns a copy of the array where each value of the new array has been
  // "cloned" from the corresponding value of this array. If this array contains
  // primitive data types, this is equivalent to simply copying the contents.
  // However, if the array contains objects, then each new element is created by
  // calling the |Clone| method of the source element, which should make a copy
  // of the element.
  //
  // Please note that calling this method will fail compilation if the element
  // type cannot be cloned (which usually means that it is a Mojo handle type or
  // a type contains Mojo handles).
  Array Clone() const {
    Array result;
    result.is_null_ = is_null_;
    Traits::Clone(vec_, &result.vec_);
    return result;
  }

  // Indicates whether the contents of this array are equal to |other|. A null
  // array is only equal to another null array. Elements are compared using the
  // |ValueTraits::Equals| method, which in most cases calls the |Equals| method
  // of the element.
  bool Equals(const Array& other) const {
    if (is_null() != other.is_null())
      return false;
    if (vec_.size() != other->size())
      return false;
    for (size_t i = 0; i < vec_.size(); ++i) {
      if (!internal::ValueTraits<T>::Equals(vec_.at(i), other->at(i)))
        return false;
    }
    return true;
  }

  // Creates a non-null array of the specified size. The elements will be
  // value-initialized (meaning that they will be initialized by their default
  // constructor, if any, or else zero-initialized).
  static Array New(size_t size) {
    Array ret;
    ret.resize(size);
    return ret;
  }

  // Creates a new array with a copy of the contents of |other|.
  template <typename U>
  static Array From(const U& other) {
    return TypeConverter<Array, U>::Convert(other);
  }

  // Copies the contents of this array to a new object of type |U|.
  template <typename U>
  U To() const {
    return TypeConverter<U, Array>::Convert(*this);
  }

 private:
  void Take(Array* other) {
    reset();
    Swap(other);
  }

  std::vector<T> vec_;
  bool is_null_;
};

template <typename T>
using VectorPtr = Array<T>;

// A |TypeConverter| that will create an |Array<T>| containing a copy of the
// contents of an |std::vector<E>|, using |TypeConverter<T, E>| to copy each
// element. The returned array will always be non-null.
template <typename T, typename E>
struct TypeConverter<Array<T>, std::vector<E>> {
  static Array<T> Convert(const std::vector<E>& input) {
    auto result = Array<T>::New(input.size());
    for (size_t i = 0; i < input.size(); ++i)
      result->at(i) = TypeConverter<T, E>::Convert(input[i]);
    return result;
  }
};

// A |TypeConverter| that will create an |std::vector<E>| containing a copy of
// the contents of an |Array<T>|, using |TypeConverter<E, T>| to copy each
// element. If the input array is null, the output vector will be empty.
template <typename E, typename T>
struct TypeConverter<std::vector<E>, Array<T>> {
  static std::vector<E> Convert(const Array<T>& input) {
    std::vector<E> result;
    if (!input.is_null()) {
      result.resize(input->size());
      for (size_t i = 0; i < input->size(); ++i)
        result[i] = TypeConverter<E, T>::Convert(input->at(i));
    }
    return result;
  }
};

// A |TypeConverter| that will create an |Array<T>| containing a copy of the
// contents of an |std::set<E>|, using |TypeConverter<T, E>| to copy each
// element. The returned array will always be non-null.
template <typename T, typename E>
struct TypeConverter<Array<T>, std::set<E>> {
  static Array<T> Convert(const std::set<E>& input) {
    Array<T> result = Array<T>::New(0u);
    for (auto i : input)
      result.push_back(TypeConverter<T, E>::Convert(i));
    return result;
  }
};

// A |TypeConverter| that will create an |std::set<E>| containing a copy of
// the contents of an |Array<T>|, using |TypeConverter<E, T>| to copy each
// element. If the input array is null, the output set will be empty.
template <typename E, typename T>
struct TypeConverter<std::set<E>, Array<T>> {
  static std::set<E> Convert(const Array<T>& input) {
    std::set<E> result;
    if (!input.is_null()) {
      for (size_t i = 0; i < input->size(); ++i)
        result.insert(TypeConverter<E, T>::Convert(input->at(i)));
    }
    return result;
  }
};

}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_ARRAY_H_
