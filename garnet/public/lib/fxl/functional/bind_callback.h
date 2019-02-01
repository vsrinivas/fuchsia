// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FUNCTIONAL_BIND_CALLBACK_H_
#define LIB_FXL_FUNCTIONAL_BIND_CALLBACK_H_

#include <functional>
#include "lib/fxl/memory/weak_ptr.h"

namespace fxl {

// Binds a weak pointer check to a callback and unwraps contents
// the callback's first argument will be the content of weak_ptr unwrapped as T&
template <typename... Args, typename T, typename Func>
decltype(auto) BindWeakUnwrap(const WeakPtr<T>& weak_ptr, Func callback) {
  return [weak_ptr, callback](Args... args) mutable {
    if (weak_ptr) {
      callback(*weak_ptr, args...);
    }
  };
}

// Binds a weak pointer to a callback
// Simply binds a callback to the lifecycle of a weak ptr
template <typename... Args, typename T, typename Func>
decltype(auto) BindWeak(const WeakPtr<T>& weak_ptr, Func callback) {
  return [weak_ptr, callback](Args... args) mutable {
    if (weak_ptr) {
      callback(args...);
    }
  };
}

// Binds a callback to a member method of object contained within weak_ptr
// Uses std::bind to bind a method pointer `method` to the object contained
// within weak_ptr
template <typename T, typename Func, typename... Args>
decltype(auto) BindWeakSelf(const WeakPtr<T>& weak_ptr, Func method,
                            Args... args) {
  auto callback = std::bind(method, weak_ptr.get(), args...);
  return [weak_ptr, callback]() {
    if (weak_ptr) {
      callback();
    }
  };
}

}  // namespace fxl

#endif  // LIB_FXL_FUNCTIONAL_BIND_CALLBACK_H_
