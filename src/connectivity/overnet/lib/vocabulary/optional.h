// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <ostream>

#include "src/connectivity/overnet/lib/vocabulary/manual_constructor.h"

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
    if (set_)
      storage_.Init(*other.storage_.get());
  }
  Optional(Optional&& other) : set_(other.set_) {
    if (set_) {
      storage_.Init(std::move(*other.storage_.get()));
    }
  }
  ~Optional() { Reset(); }
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
    if (!set_ && !other->set_)
      return;
    if (set_ && other->set_) {
      auto tmp = std::move(*storage_.get());
      *storage_.get() = std::move(*other->storage_.get());
      *other->storage_.get() = std::move(tmp);
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
  Optional& operator=(const T& other) {
    Optional(other).Swap(this);
    return *this;
  }
  Optional& operator=(T&& other) {
    if (set_) {
      *storage_.get() = std::move(other);
    } else {
      set_ = true;
      storage_.Init(std::forward<T>(other));
    }
    return *this;
  }
  T* operator->() {
    assert(set_);
    return storage_.get();
  }
  const T* operator->() const {
    assert(set_);
    return storage_.get();
  }
  T& operator*() {
    assert(set_);
    return *storage_.get();
  }
  const T& operator*() const {
    assert(set_);
    return *storage_.get();
  }
  typedef bool Optional::*FakeBool;
  operator FakeBool() const { return set_ ? &Optional::set_ : nullptr; }
  bool has_value() const { return set_; }
  T& value() {
    assert(set_);
    return *storage_.get();
  }
  const T& value() const {
    assert(set_);
    return *storage_.get();
  }
  T* get() { return set_ ? storage_.get() : nullptr; }
  const T* get() const { return set_ ? storage_.get() : nullptr; }

  T* Force() {
    if (!set_) {
      set_ = true;
      storage_.Init();
    }
    return get();
  }

  T Take() {
    assert(set_);
    set_ = false;
    T out = std::move(*storage_.get());
    storage_.Destroy();
    return std::move(out);
  }

  template <typename F>
  auto Map(F f) const -> Optional<decltype(f(*get()))> const {
    if (!set_)
      return Nothing;
    return f(*storage_.get());
  }

  template <typename F>
  auto Then(F f) const -> decltype(f(*get())) {
    using Returns = decltype(f(*get()));
    if (!set_)
      return Returns();
    return f(*storage_.get());
  }

  template <typename F>
  auto Then(F f) -> decltype(f(*get())) {
    using Returns = decltype(f(*get()));
    if (!set_)
      return Returns();
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

template <class T>
bool operator==(const Optional<T>& a, const T& b) {
  if (!a.has_value())
    return false;
  return a.value() == b;
}

template <class T>
bool operator==(const T& a, const Optional<T>& b) {
  if (!b.has_value())
    return false;
  return b.value() == a;
}

template <class T>
bool operator!=(const Optional<T>& a, const T& b) {
  return !operator==(a, b);
}

template <class T>
bool operator!=(const T& a, const Optional<T>& b) {
  return !operator==(a, b);
}

template <class T, class U>
bool operator!=(const Optional<T>& a, const Optional<U>& b) {
  return !operator==(a, b);
}

template <class T>
std::ostream& operator<<(std::ostream& out, const Optional<T>& val) {
  if (!val)
    return out << "(nil)";
  return out << *val.get();
}

}  // namespace overnet
