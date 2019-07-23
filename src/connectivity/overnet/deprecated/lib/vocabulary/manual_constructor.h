// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>
#include <utility>

namespace overnet {

//#define OVERNET_UNIQUE_PTR_MANUAL_CONSTRUCTOR

template <typename Type>
class ManualConstructor {
 public:
  ManualConstructor() {}
  ~ManualConstructor() {}
  ManualConstructor(const ManualConstructor&) = delete;
  ManualConstructor& operator=(const ManualConstructor&) = delete;

  Type* get() { return reinterpret_cast<Type*>(space()); }
  const Type* get() const { return reinterpret_cast<const Type*>(space()); }

  Type* operator->() { return get(); }
  const Type* operator->() const { return get(); }

  Type& operator*() { return *get(); }
  const Type& operator*() const { return *get(); }

  void Init() { new (Materialize()) Type; }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // Note that Init() with no arguments performs default-initialization,
  // not zero-initialization (i.e it behaves the same as "new Type;", not
  // "new Type();"), so it will leave non-class types uninitialized.
  template <typename... Ts>
  void Init(Ts&&... args) {
    new (Materialize()) Type(std::forward<Ts>(args)...);
  }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  void Init(const Type& x) { new (Materialize()) Type(x); }
  void Init(Type&& x) { new (Materialize()) Type(std::move(x)); }

  void Destroy() {
    get()->~Type();
    Dematerialize();
  }

 private:
#ifndef OVERNET_UNIQUE_PTR_MANUAL_CONSTRUCTOR
  union {
    Type space_;
  };
  Type* space() { return &space_; }
  const Type* space() const { return &space_; }
  Type* Materialize() { return &space_; }
  void Dematerialize() {}
#else
  typedef std::aligned_storage_t<sizeof(Type), alignof(Type)> Storage;
  std::unique_ptr<Storage> storage_;
  Storage* space() {
    assert(storage_ != nullptr);
    return storage_.get();
  }
  const Storage* space() const {
    assert(storage_ != nullptr);
    return storage_.get();
  }
  Storage* Materialize() {
    assert(storage_ == nullptr);
    storage_.reset(new Storage);
    return storage_.get();
  }
  void Dematerialize() {
    assert(storage_ != nullptr);
    storage_.reset();
  }
#endif
};

}  // namespace overnet
