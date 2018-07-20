// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_CAPTURE_H_
#define LIB_CALLBACK_CAPTURE_H_

#include <functional>
#include <utility>

namespace callback {
namespace internal {

template <typename C, typename... T>
class CaptureLambda {};

template <typename C>
class CaptureLambda<C> {
 public:
  explicit CaptureLambda(C callback) : callback_(std::move(callback)) {}

  void operator()() { callback_(); }

 private:
  C callback_;
};

template <typename C, typename T1, typename... T>
class CaptureLambda<C, T1, T...> {
 public:
  CaptureLambda(C callback, T1* t1, T*... args)
      : t1_(t1), sub_lambda_(std::move(callback), args...) {}

  template <typename V1, typename... V>
  void operator()(V1 v1, V&&... args) {
    if (t1_)
      *t1_ = std::move(v1);
    sub_lambda_(std::forward<V>(args)...);
  }

 private:
  T1* const t1_;
  CaptureLambda<C, T...> sub_lambda_;
};

}  // namespace internal

// Takes a callback, and a list of pointers. Returns a lambda that takes a list
// of objects, saves these in the pointed variables and runs the callback.
template <typename C, typename... T>
auto Capture(C callback, T*... ptrs) {
  return internal::CaptureLambda<C, T...>(std::move(callback), ptrs...);
}

}  // namespace callback

#endif  // LIB_CALLBACK_CAPTURE_H_
