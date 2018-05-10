// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <type_traits>
#include <utility>

namespace overnet {

// Polyfill for std::optional... remove once that becomes available.
template <class T>
class Optional {
 public:
  using element_type = T;

  Optional() : set_(false) {}
  Optional(const T& value) : set_(true) { new (&storage_) T(value); }
  Optional(T&& value) : set_(true) { new (&storage_) T(std::move(value)); }
  Optional(const Optional& other) : set_(other.set_) {
    if (set_) new (&storage_) T(*other.stored());
  }
  Optional(Optional&& other) : set_(other.set_) {
    if (set_) {
      new (&storage_) T(std::move(*other.stored()));
    }
  }
  ~Optional() {
    if (set_) stored()->~T();
  }
  void Swap(Optional* other) {
    if (!set_ && !other->set_) return;
    if (set_ && other->set_) {
      using std::swap;
      swap(*stored(), *other->stored());
      return;
    }
    if (set_) {
      assert(!other->set_);
      new (&other->storage_) T(std::move(*stored()));
      stored()->~T();
      set_ = false;
      other->set_ = true;
      return;
    }
    assert(!set_ && other->set_);
    new (&storage_) T(std::move(*other->stored()));
    other->stored()->~T();
    set_ = true;
    other->set_ = false;
    return;
  }
  Optional& operator=(const Optional& other) {
    Optional(other).Swap(this);
    return *this;
  }
  Optional& operator=(Optional&& other) {
    Swap(&other);
    return *this;
  }
  T* operator->() { return stored(); }
  const T* operator->() const { return stored(); }
  T& operator*() { return *stored(); }
  const T& operator*() const { return *stored(); }
  operator bool() const { return set_; }
  bool has_value() const { return set_; }
  T& value() { return *stored(); }
  const T& value() const { return *stored(); }
  T* get() { return set_ ? stored() : nullptr; }
  const T* get() const { return set_ ? stored() : nullptr; }

 private:
  bool set_;
  const T* stored() const {
    assert(set_);
    return reinterpret_cast<const T*>(&storage_);
  }
  T* stored() {
    assert(set_);
    return reinterpret_cast<T*>(&storage_);
  }
  std::aligned_storage_t<sizeof(T), alignof(T)> storage_;
};

}  // namespace overnet