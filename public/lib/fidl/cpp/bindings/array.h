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

namespace fidl {

// Represents a moveable array with contents of type |T|. The array can be null,
// meaning that no value has been assigned to it. Null is distinct from empty.
template <typename T>
class Array {
 public:
  using ConstRefType = typename std::vector<T>::const_reference;
  using RefType = typename std::vector<T>::reference;

  using Traits = internal::ArrayTraits<T, internal::IsMoveOnlyType<T>::value>;
  using ForwardType = typename Traits::ForwardType;

  typedef internal::Array_Data<typename internal::WrapperTraits<T>::DataType>
      Data_;

  // Constructs a new array that is null.
  Array() : is_null_(true) {}

  // Makes null arrays implicitly constructible from |nullptr|.
  Array(std::nullptr_t) : is_null_(true) {}

  ~Array() {}

  // Moves the contents of |other| into this array.
  Array(Array&& other) : is_null_(true) { Take(&other); }
  Array& operator=(Array&& other) {
    Take(&other);
    return *this;
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

  // Resets the contents of this array back to null.
  void reset() {
    vec_.clear();
    is_null_ = true;
  }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  // Indicates whether the array is null (which is distinct from empty).
  bool is_null() const { return is_null_; }

  // Returns a reference to the first element of the array. Calling this on a
  // null or empty array causes undefined behavior.
  ConstRefType front() const { return vec_.front(); }
  RefType front() { return vec_.front(); }

  // Returns the size of the array, which will be zero if the array is null.
  size_t size() const { return vec_.size(); }

  // For non-null arrays of non-bool types, returns a pointer to the first
  // element, if any. (If the array is empty, the semantics are the same as for
  // |std::vector<T>::data()|. The behavior is undefined if the array is null.)
  const T* data() const { return vec_.data(); }
  T* data() { return vec_.data(); }

  // Returns a reference to the element at zero-based |offset|. Calling this on
  // an array with size less than |offset|+1 causes undefined behavior.
  ConstRefType at(size_t offset) const { return vec_.at(offset); }
  ConstRefType operator[](size_t offset) const { return at(offset); }
  RefType at(size_t offset) { return vec_.at(offset); }
  RefType operator[](size_t offset) { return at(offset); }

  // Pushes |value| onto the back of the array. If this array was null, it will
  // become non-null with a size of 1.
  void push_back(ForwardType value) {
    is_null_ = false;
    Traits::PushBack(&vec_, value);
  }

  // Resizes the array to |size| and makes it non-null. Otherwise, works just
  // like the resize method of |std::vector|.
  void resize(size_t size) {
    is_null_ = false;
    vec_.resize(size);
  }

  // Returns a const reference to the |std::vector| managed by this class. If
  // the array is null, this will be an empty vector.
  const std::vector<T>& storage() const { return vec_; }
  operator const std::vector<T>&() const { return vec_; }

  // Swaps the contents of this array with the |other| array, including
  // nullness.
  void Swap(Array* other) {
    std::swap(is_null_, other->is_null_);
    vec_.swap(other->vec_);
  }

  // Swaps the contents of this array with the specified vector, making this
  // array non-null. Since the vector cannot represent null, it will just be
  // made empty if this array is null.
  void Swap(std::vector<T>* other) {
    is_null_ = false;
    vec_.swap(*other);
  }

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
    if (size() != other.size())
      return false;
    for (size_t i = 0; i < size(); ++i) {
      if (!internal::ValueTraits<T>::Equals(at(i), other.at(i)))
        return false;
    }
    return true;
  }

 public:
  // Array<>::Iterator satisfies the RandomAccessIterator concept:
  //   http://en.cppreference.com/w/cpp/concept/RandomAccessIterator.
  class Iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = T*;
    using reference = T&;

    // The following satisfy BidirectionalIterator:
    Iterator() : arr_(nullptr), pos_(0u) {}
    Iterator(Array<T>* arr, size_t pos) : arr_(arr), pos_(pos) {}
    Iterator& operator++() {
      ++pos_;
      return *this;
    }
    Iterator operator++(int) {
      Iterator original = *this;
      ++pos_;
      return original;
    }
    Iterator& operator--() {
      --pos_;
      return *this;
    }
    Iterator operator--(int) {
      Iterator original = *this;
      --pos_;
      return original;
    }
    bool operator==(const Iterator& o) const {
      return o.arr_ == arr_ && o.pos_ == pos_;
    }
    bool operator!=(const Iterator& o) const { return !(*this == o); }
    RefType operator*() const { return arr_->at(pos_); }
    T* operator->() const { return &arr_->at(pos_); }

    // The following satisfy RandomAccessIterator:
    Iterator& operator+=(difference_type dist) {
      pos_ += dist;
      return *this;
    }
    Iterator& operator-=(difference_type dist) {
      pos_ -= dist;
      return *this;
    }
    friend Iterator operator+(difference_type dist, const Iterator& o_it) {
      return Iterator(o_it.arr_, dist + o_it.pos_);
    }
    Iterator operator+(difference_type dist) const {
      return Iterator(arr_, pos_ + dist);
    }
    Iterator operator-(difference_type dist) const {
      return Iterator(arr_, pos_ - dist);
    }
    difference_type operator-(const Iterator& o_it) const {
      return pos_ - o_it.pos_;
    }
    bool operator<(const Iterator& o_it) const { return pos_ < o_it.pos_; }
    bool operator>(const Iterator& o_it) const { return pos_ > o_it.pos_; }
    bool operator<=(const Iterator& o_it) const { return pos_ <= o_it.pos_; }
    bool operator>=(const Iterator& o_it) const { return pos_ >= o_it.pos_; }
    RefType operator[](difference_type dist) { return arr_->at(pos_ + dist); }

   private:
    Array<T>* arr_;
    size_t pos_;
  };

  Iterator begin() { return Iterator(this, 0); }
  Iterator end() { return Iterator(this, size()); }

 private:
  void Take(Array* other) {
    reset();
    Swap(other);
  }

  std::vector<T> vec_;
  bool is_null_;

  FIDL_MOVE_ONLY_TYPE(Array);
};

// A |TypeConverter| that will create an |Array<T>| containing a copy of the
// contents of an |std::vector<E>|, using |TypeConverter<T, E>| to copy each
// element. The returned array will always be non-null.
template <typename T, typename E>
struct TypeConverter<Array<T>, std::vector<E>> {
  static Array<T> Convert(const std::vector<E>& input) {
    auto result = Array<T>::New(input.size());
    for (size_t i = 0; i < input.size(); ++i)
      result[i] = TypeConverter<T, E>::Convert(input[i]);
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
      result.resize(input.size());
      for (size_t i = 0; i < input.size(); ++i)
        result[i] = TypeConverter<E, T>::Convert(input[i]);
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
      for (size_t i = 0; i < input.size(); ++i)
        result.insert(TypeConverter<E, T>::Convert(input[i]));
    }
    return result;
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_ARRAY_H_
