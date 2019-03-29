// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_ENSURE_CALLED_H_
#define LIB_CALLBACK_ENSURE_CALLED_H_

#include <optional>
#include <tuple>
#include <utility>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace callback {

template <typename T, typename... ArgType>
class EnsureCalled {
 public:
  EnsureCalled() = default;

  explicit EnsureCalled(T&& function, ArgType&&... args)
      : closure_(std::pair(
            std::move(function),
            std::tuple<ArgType...>(std::forward<ArgType>(args)...))){};

  EnsureCalled(EnsureCalled&& other) { *this = std::move(other); }

  EnsureCalled& operator=(EnsureCalled&& other) {
    CallDefaultIfNeeded();

    // Assigning |closure_| directly does not work because lambdas are
    // move-constructible but not move-assignable. We construct a new pair in
    // place instead.
    if (other.closure_) {
      closure_.emplace(*other.TakeClosure());
    }
    return *this;
  }

  ~EnsureCalled() { CallDefaultIfNeeded(); }

  auto operator()(ArgType... args) {
    FXL_DCHECK(closure_);
    auto closure = TakeClosure();
    return std::invoke(std::move(closure->first),
                       std::forward<ArgType>(args)...);
  }

  explicit operator bool() const { return closure_.has_value(); }

 private:
  void CallDefaultIfNeeded() {
    if (!closure_) {
      return;
    }

    auto closure = TakeClosure();
    std::apply(std::move(closure->first), std::move(closure->second));
  }

  std::optional<std::pair<T, std::tuple<ArgType...>>> TakeClosure() {
    auto closure = std::move(closure_);
    closure_ = std::nullopt;
    return closure;
  }

  std::optional<std::pair<T, std::tuple<ArgType...>>> closure_;
};

}  // namespace callback

#endif  // LIB_CALLBACK_ENSURE_CALLED_H_
