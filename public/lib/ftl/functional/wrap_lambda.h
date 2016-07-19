// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FUNCTIONAL_LAMBDA_WRAPPER_H_
#define LIB_FTL_FUNCTIONAL_LAMBDA_WRAPPER_H_

#include <utility>

#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"

namespace ftl {
namespace internal {

template <typename T>
class LambdaWrapper {
 public:
  explicit LambdaWrapper(T func)
      : impl_(MakeRefCounted<Impl>(std::move(func))) {}

  template <typename... ArgType>
  auto operator()(ArgType&&... args) {
    return impl_->func_(std::forward<ArgType>(args)...);
  }

 private:
  class Impl : public RefCountedThreadSafe<Impl> {
   public:
    explicit Impl(T func) : func_(std::move(func)) {}
    T func_;
  };

  RefPtr<Impl> impl_;
};

}  // namespace internal

// Provides a wrapper for a move-only lambda that is implictly convertable to an
// std::function.
//
// std::function is copyable, but if a lambda captures an argument with a
// move-only type, the lambda itself is not copyable. In order to use the lambda
// in places that accept std::functions, we provide a copyable object that wraps
// the lambda and is implicitly convertable to an std::function.
//
// EXAMPLE:
//
// std::unique_ptr<Foo> foo = ...
// std::function<int()> func =
//     ftl::WrapLambda([bar = std::move(foo)]() { return bar->count(); });
//
// Notice that the return type of WrapLambda is rarely used directly. Instead,
// callers typically erase the type by implicitly converting the return value
// to an std::function.
template <typename T>
internal::LambdaWrapper<T> WrapLambda(T lambda) {
  return internal::LambdaWrapper<T>(std::move(lambda));
}

}  // namespace ftl

#endif  // LIB_FTL_FUNCTIONAL_LAMBDA_WRAPPER_H_
