// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_SCOPED_CALLBACK_H_
#define LIB_CALLBACK_SCOPED_CALLBACK_H_

#include <utility>

namespace callback {
namespace internal {

template <typename W, typename T>
class ScopedLambda {
 public:
  explicit ScopedLambda(W witness, T function)
      : witness_(std::move(witness)), function_(std::move(function)) {}

  template <typename... ArgType>
  void operator()(ArgType&&... args) {
    if (witness_) {
      function_(std::forward<ArgType>(args)...);
    }
  }

  template <typename... ArgType>
  void operator()(ArgType&&... args) const {
    if (witness_) {
      function_(std::forward<ArgType>(args)...);
    }
  }

 private:
  W witness_;
  T function_;
};

}  // namespace internal

// Scopes the given |lambda| to the given |witness|.
//
// This returns a new callable with the same signature as the original one.
// Calling the new callable will be forwared to |lambda| if and only if
// |witness| is true at the time where the callbable is called.
template <typename W, typename T>
internal::ScopedLambda<W, T> MakeScoped(W witness, T lambda) {
  return internal::ScopedLambda<W, T>(std::move(witness), std::move(lambda));
}

}  // namespace callback

#endif  // LIB_CALLBACK_SCOPED_CALLBACK_H_
