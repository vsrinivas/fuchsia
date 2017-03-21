// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_DESTRUCTION_GUARD_H_
#define APPS_LEDGER_SRC_CALLBACK_DESTRUCTION_GUARD_H_

#include <functional>
#include <utility>

#include "lib/ftl/functional/closure.h"

namespace callback {

template <typename C>
class DestructionGuard {
 public:
  DestructionGuard() : null_(true) {}

  explicit DestructionGuard(C callback)
      : null_(false), callback_(std::move(callback)) {}

  DestructionGuard(DestructionGuard<C>&& other)
      : null_(other.null_), callback_(std::move(other.callback_)) {
    other.null_ = true;
  }

  ~DestructionGuard() {
    if (!null_) {
      callback_();
    }
  }

  DestructionGuard& operator=(DestructionGuard<C>&& other) {
    callback_ = std::move(other.callback_);
    null_ = other.null_;
    other.null_ = true;
    return *this;
  }

  void Reset(std::nullptr_t null = nullptr) { null_ = true; }

  void Reset(C callback) {
    null_ = false;
    callback_ = std::move(callback);
  }

 private:
  // Whether no closure is set on the guard. This is necessary because not all
  // callable are convertible to boolean.
  bool null_;
  C callback_;
};

template <typename C>
DestructionGuard<C> MakeDestructionGuard(C&& callback) {
  return DestructionGuard<C>(std::forward<C>(callback));
}

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_DESTRUCTION_GUARD_H_
