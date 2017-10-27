// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>
#include <utility>

namespace bluetooth {
namespace common {

// This class provides functionality that is similar to that of C++17's
// std::optional but in a way that is more limited.
//
// TODO(armansito): Get rid of this and use std::optional when it's available.
template <typename T>
class Optional final {
 public:
  Optional() : ptr_(nullptr) {}

  // Copy and assignment operators.
  Optional(const Optional& other) {
    if (other) {
      value_ = other.value_;
      ptr_ = &value_;
    } else {
      Reset();
    }
  }

  Optional(Optional&& other) {
    if (other) {
      value_ = std::move(other.value_);
      ptr_ = &value_;
    } else {
      Reset();
    }
  }

  Optional& operator=(const Optional& other) {
    if (other) {
      value_ = other.value_;
      ptr_ = &value_;
    } else {
      Reset();
    }
    return *this;
  }

  Optional& operator=(Optional&& other) {
    if (other) {
      value_ = std::move(other.value_);
      ptr_ = &value_;
    } else {
      Reset();
    }
    return *this;
  }

  // Assigns contents. The compile-time conditions used here are documented in
  // http://en.cppreference.com/w/cpp/utility/optional/operator%3D.
  template <typename U = T,
            typename = typename std::enable_if<
                std::is_same<typename std::decay<U>::type, T>::value &&
                std::is_constructible<T, U>::value &&
                std::is_assignable<T&, U>::value>::type>
  Optional& operator=(U&& value) {
    value_ = std::forward<U>(value);
    ptr_ = &value_;
    return *this;
  }

  // (In)equality operators
  // We are the same as another Optional if both hold no value, or they hold a
  // value that is equal.
  bool operator==(const Optional& other) const {
    return (*this && other && (**this == *other)) || (!*this && !other);
  }
  bool operator!=(const Optional& other) const { return !(*this == other); };

  // Returns true if this object has a value.
  constexpr bool HasValue() const { return ptr_ != nullptr; }
  constexpr explicit operator bool() const { return HasValue(); }

  // Returns the contained value or nullptr if a value does not exist.
  constexpr T* value() const { return ptr_; }
  constexpr const T* operator->() const { return ptr_; }
  constexpr T* operator->() { return ptr_; }

  // Operators for accessing the contained value. Behavior is undefined if
  // |this| does not contain a value. These can only be called on a lvalue.
  constexpr const T& operator*() const& { return *ptr_; }
  constexpr T& operator*() & { return *ptr_; }

  // Resets the contents.
  void Reset() {
    ptr_ = nullptr;
    value_ = T();
  }

 private:
  T* ptr_;
  T value_;
};

}  // namespace common
}  // namespace bluetooth
