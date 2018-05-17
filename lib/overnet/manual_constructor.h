// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>
#include <utility>

namespace overnet {

template <typename Type>
class ManualConstructor {
 public:
  ManualConstructor() {}
  ~ManualConstructor() {}
  ManualConstructor(const ManualConstructor&) = delete;
  ManualConstructor& operator=(const ManualConstructor&) = delete;

  Type* get() { return reinterpret_cast<Type*>(&space_); }
  const Type* get() const { return reinterpret_cast<const Type*>(&space_); }

  Type* operator->() { return get(); }
  const Type* operator->() const { return get(); }

  Type& operator*() { return *get(); }
  const Type& operator*() const { return *get(); }

  void Init() { new (&space_) Type; }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // Note that Init() with no arguments performs default-initialization,
  // not zero-initialization (i.e it behaves the same as "new Type;", not
  // "new Type();"), so it will leave non-class types uninitialized.
  template <typename... Ts>
  void Init(Ts&&... args) {
    new (&space_) Type(std::forward<Ts>(args)...);
  }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  void Init(const Type& x) { new (&space_) Type(x); }
  void Init(Type&& x) { new (&space_) Type(std::move(x)); }

  void Destroy() { get()->~Type(); }

 private:
  union {
    Type space_;
  };
};

}  // namespace overnet
