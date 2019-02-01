// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FUNCTIONAL_MAKE_COPYABLE_H_
#define LIB_FXL_FUNCTIONAL_MAKE_COPYABLE_H_

#include <utility>

#include <lib/fit/function.h>

#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace fxl {
namespace internal {

template <typename T>
class CopyableLambda {
 public:
  explicit CopyableLambda(T func)
      : impl_(MakeRefCounted<Impl>(std::move(func))) {}

  template <typename... ArgType>
  auto operator()(ArgType&&... args) const {
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

// Provides a wrapper for a move-only lambda that is implicitly convertible to
// an fit::function.
//
// fit::function is copyable, but if a lambda captures an argument with a
// move-only type, the lambda itself is not copyable. In order to use the lambda
// in places that accept fit::functions, we provide a copyable object that wraps
// the lambda and is implicitly convertible to an fit::function.
//
// EXAMPLE:
//
// std::unique_ptr<Foo> foo = ...
// fit::function<int()> func =
//     fxl::MakeCopyable([bar = std::move(foo)]() { return bar->count(); });
//
// Notice that the return type of MakeCopyable is rarely used directly. Instead,
// callers typically erase the type by implicitly converting the return value
// to an fit::function.
template <typename T>
internal::CopyableLambda<T> MakeCopyable(T lambda) {
  return internal::CopyableLambda<T>(std::move(lambda));
}

}  // namespace fxl

#endif  // LIB_FXL_FUNCTIONAL_MAKE_COPYABLE_H_
