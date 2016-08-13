// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FUNCTIONAL_MAKE_RUNNABLE_H_
#define LIB_FTL_FUNCTIONAL_MAKE_RUNNABLE_H_

#include <utility>

namespace ftl {
namespace internal {

template <typename T>
class RunnableAdaptor {
 public:
  explicit RunnableAdaptor(T func) : func_(std::move(func)) {}

  template <typename... ArgType>
  void Run(ArgType&&... args) const {
    return func_(std::forward<ArgType>(args)...);
  }

 private:
  T func_;
};

}  // namespace internal

// Provides a wrapper for a move-only lambda that is implictly convertable to
// mojo::Callback.
template <typename T>
internal::RunnableAdaptor<T> MakeRunnable(T lambda) {
  return internal::RunnableAdaptor<T>(std::move(lambda));
}

}  // namespace ftl

#endif  // LIB_FTL_FUNCTIONAL_MAKE_RUNNABLE_H_
