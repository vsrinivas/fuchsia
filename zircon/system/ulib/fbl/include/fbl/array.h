// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ARRAY_H_
#define FBL_ARRAY_H_

#include <zircon/assert.h>
#include <fbl/macros.h>
#include <type_traits>

namespace fbl {

// |Array| is lightweight movable container that takes ownership of an array.
// At destruction or when reset() is invoked, it uses delete[] to release the owned array.
template <typename T>
class Array {
 public:
  constexpr Array() : ptr_(nullptr), count_(0U) {}
  constexpr Array(decltype(nullptr)) : Array() {}

  Array(T* array, size_t count) : ptr_(array), count_(count) {}

  Array(Array&& other) : ptr_(nullptr), count_(other.count_) { ptr_ = other.release(); }

  // Move constructor allowing us to move from Array<T> to Array<const T>
  template <typename OtherType>
  Array(Array<OtherType>&& other) : ptr_(nullptr), count_(other.size()) {
    static_assert(!std::is_const<OtherType>::value && std::is_same<const OtherType, T>::value);

    ptr_ = other.release();
  }

  size_t size() const { return count_; }

  ~Array() { reset(); }

  Array& operator=(Array&& o) {
    auto count = o.count_;
    reset(o.release(), count);
    return *this;
  }

  // Move operator allowing us to move from Array<T> to Array<const T>
  template <typename OtherType>
  Array& operator=(Array<OtherType>&& o) {
    static_assert(!std::is_const<OtherType>::value && std::is_same<const OtherType, T>::value);

    auto count = o.size();
    reset(o.release(), count);
    return *this;
  }

  // move semantics only
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Array);

  T* release() {
    T* t = ptr_;
    ptr_ = nullptr;
    count_ = 0;
    return t;
  }

  void reset() { reset(nullptr, 0U); }

  void reset(T* t, size_t count) {
    T* ptr = ptr_;
    ptr_ = t;
    count_ = count;
    delete[] ptr;
  }

  void swap(Array& other) {
    T* t = ptr_;
    ptr_ = other.ptr_;
    other.ptr_ = t;
    size_t c = count_;
    count_ = other.count_;
    other.count_ = c;
  }

  T* get() const { return ptr_; }

  T* data() const { return ptr_; }

  explicit operator bool() const { return static_cast<bool>(ptr_); }

  T& operator[](size_t i) const {
    ZX_DEBUG_ASSERT(i < count_);
    return ptr_[i];
  }

  T* begin() const { return ptr_; }

  T* end() const { return &ptr_[count_]; }

 private:
  T* ptr_;
  size_t count_;
};

}  // namespace fbl

#endif  // FBL_ARRAY_H_
