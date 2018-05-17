// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ostream>
#include "manual_constructor.h"

namespace overnet {

enum NothingType { Nothing };

// Polyfill for std::optional... remove once that becomes available.
template <class T>
class Optional {
 public:
  using element_type = T;

  Optional() : set_(false) {}
  Optional(NothingType) : set_(false) {}
  Optional(const T& value) : set_(true) { storage_.Init(value); }
  Optional(T&& value) : set_(true) { storage_.Init(std::move(value)); }
  Optional(const Optional& other) : set_(other.set_) {
    if (set_) storage_.Init(*other.storage_.get());
  }
  Optional(Optional&& other) : set_(other.set_) {
    if (set_) {
      storage_.Init(std::move(*other.storage_.get()));
    }
  }
  ~Optional() {
    if (set_) storage_.Destroy();
  }
  void Reset() {
    if (set_) {
      set_ = false;
      storage_.Destroy();
    }
  }
  template <typename... Arg>
  void Reset(Arg&&... args) {
    if (set_) {
      storage_.Destroy();
    }
    set_ = true;
    storage_.Init(std::forward<Arg>(args)...);
  }
  void Swap(Optional* other) {
    if (!set_ && !other->set_) return;
    if (set_ && other->set_) {
      using std::swap;
      swap(*storage_.get(), *other->storage_.get());
      return;
    }
    if (set_) {
      assert(!other->set_);
      new (&other->storage_) T(std::move(*storage_.get()));
      storage_.Destroy();
      set_ = false;
      other->set_ = true;
      return;
    }
    assert(!set_ && other->set_);
    storage_.Init(std::move(*other->storage_.get()));
    other->storage_.Destroy();
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
  T* operator->() { return storage_.get(); }
  const T* operator->() const { return storage_.get(); }
  T& operator*() { return *storage_.get(); }
  const T& operator*() const { return *storage_.get(); }
  operator bool() const { return set_; }
  bool has_value() const { return set_; }
  T& value() { return *storage_.get(); }
  const T& value() const { return *storage_.get(); }
  T* get() { return set_ ? storage_.get() : nullptr; }
  const T* get() const { return set_ ? storage_.get() : nullptr; }

  T Take() {
    assert(set_);
    set_ = false;
    T out = std::move(*storage_.get());
    storage_.Destroy();
    return std::move(out);
  }

  template <typename F>
  auto Map(F f) -> Optional<decltype(f(*get()))> const {
    if (!set_) return Nothing;
    return f(*storage_.get());
  }

  template <typename F>
  auto Then(F f) const -> decltype(f(*get())) {
    using Returns = decltype(f(*get()));
    if (!set_) return Returns();
    return f(*storage_.get());
  }

  template <typename F>
  auto Then(F f) -> decltype(f(*get())) {
    using Returns = decltype(f(*get()));
    if (!set_) return Returns();
    return f(*storage_.get());
  }

  T ValueOr(T x) const { return set_ ? *get() : x; }

 private:
  bool set_;
  ManualConstructor<T> storage_;
};

template <class T, class U>
bool operator==(const Optional<T>& a, const Optional<U>& b) {
  return a.has_value() ? (b.has_value() && a.value() == b.value())
                       : !b.has_value();
}

template <class T, class U>
bool operator!=(const Optional<T>& a, const Optional<U>& b) {
  return !operator==(a, b);
}

template <class T>
std::ostream& operator<<(std::ostream& out, const Optional<T>& val) {
  if (!val) return out << "(nil)";
  return out << *val.get();
}

}  // namespace overnet
