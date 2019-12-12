// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_CAPTURE_H_
#define SRC_LEDGER_LIB_CALLBACK_CAPTURE_H_

#include <functional>
#include <tuple>
#include <utility>

namespace ledger {

// Takes a callback, and a list of pointers. Returns a lambda that takes a list
// of objects, saves these in the pointed variables and runs the callback.
// If one wants to ignore one of the captured value, one can pass &std::ignore.
template <typename C, typename... T>
auto Capture(C callback, T*... ptrs) {
  return [callback = std::move(callback), ptrs...](auto&&... values) mutable {
    std::tie(*ptrs...) = std::make_tuple(std::forward<decltype(values)>(values)...);
    callback();
  };
}

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_CAPTURE_H_
