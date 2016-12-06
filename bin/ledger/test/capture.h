// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_CAPTURE_H_
#define APPS_LEDGER_SRC_TEST_CAPTURE_H_

#include <functional>
#include <utility>

#include "lib/ftl/functional/closure.h"

namespace test {
namespace internal {

template <typename... T>
class CaptureLambda {};

template <>
class CaptureLambda<> {
 public:
  CaptureLambda(ftl::Closure callback) : callback_(std::move(callback)) {}

  void operator()() { callback_(); }

 private:
  ftl::Closure callback_;
};

template <typename T1, typename... T>
class CaptureLambda<T1, T...> {
 public:
  CaptureLambda(ftl::Closure callback, T1* t1, T*... args)
      : t1_(t1), sub_lambda_(std::move(callback), args...) {}

  template <typename V1, typename... V>
  void operator()(V1 v1, V&&... args) {
    if (t1_)
      *t1_ = std::move(v1);
    sub_lambda_(std::forward<V>(args)...);
  }

 private:
  T1* const t1_;
  CaptureLambda<T...> sub_lambda_;
};

}  // namespace internal

// Takes a callback, and a list of pointers. Returns a lambda that takes a list
// of objects, saves these in the pointed variables and runs the callback.
template <typename... T>
auto Capture(ftl::Closure callback, T*... ptrs) {
  return internal::CaptureLambda<T...>(std::move(callback), ptrs...);
}

}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_CAPTURE_H_
