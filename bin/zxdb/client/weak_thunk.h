// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

namespace zxdb {

// Use to get a weak pointer to "this" without having to require the class is
// managed by a std::shared_ptr. It is not threadsafe.
//
// In the future with C++17 it can be replaced by deriving from
// enable_shared_from_this and calling weak_from_this().
//
// In the header of the class you need a weak pointer to:
//
//   std::shared_ptr<WeakThunk<MyClass>> weak_;
//
// In the constructor of the class:
//
//   : weak_(std::make_shared<WeakThunk<MyClass>>(this))
//
// When you want to get a weak pointer:
//
//   std::weak_ptr<WeakThunk<MyClass>> my_weak_ptr(weak_);
//
// To check and dereference the weak pointer:
//
//   if (auto ptr = my_weak_ptr.lock()) {
//     ptr->thunk->DoFoo();
//   }
//
// Implementation note: This class will work without deriving from
// enable_shared_from_this but that will require shared_ptr heap-allocate the
// tracking information. Since this class will only be used in shared_ptr
// contexts, this derivation saves on a heap allocation.
template<typename Class>
struct WeakThunk : public std::enable_shared_from_this<WeakThunk<Class>> {
  WeakThunk(Class* t) : thunk(t) {}

  Class* thunk;
};

}  // namespace zxdb
