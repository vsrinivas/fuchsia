// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_HANGING_GET_HELPER_HANGING_GET_HELPER_H_
#define SRC_CAMERA_LIB_HANGING_GET_HELPER_HANGING_GET_HELPER_H_

#include <lib/fit/function.h>
#include <zircon/assert.h>

#include <optional>
#include <type_traits>

namespace camera {

// A helper for implementing the "hanging get" pattern in fidl protocols. For increased client
// simplicity, the helper invokes callbacks only when the most recent value differs from the
// previously sent one, defined by a caller-provided equality function. The helper is also usable
// with non-movable types, for which equality with previously sent values is assumed false.

// TODO(fxbug.dev/47611): convert to generic fuchsia lib
template <class T, class Compare = std::equal_to<T>, bool = std::is_copy_constructible<T>::value>
class HangingGetHelper;

template <class T, class Compare>
class HangingGetHelper<T, Compare, true> {
 public:
  HangingGetHelper(Compare eq = Compare()) : eq_(std::move(eq)) {}

  // Sets or updates the saved callback to the provided value. Returns true iff an existing callback
  // was overwritten.
  bool Get(fit::function<void(T)> callback) {
    if (callback_) {
      ZX_DEBUG_ASSERT(!pending_);
      callback_ = std::move(callback);
      return true;
    }

    if (pending_) {
      ZX_DEBUG_ASSERT(!callback_);
      callback(pending_.value());
      sent_ = pending_;
      pending_ = std::nullopt;
      return false;
    }

    callback_ = std::move(callback);
    return false;
  }

  // Sets or updates the pending value to the provided value if it differs from a previously sent
  // value. Returns true iff an existing value was overwritten.
  bool Set(T value) {
    if ((sent_ && eq_(sent_.value(), value)) || (pending_ && eq_(pending_.value(), value))) {
      return false;
    }

    if (pending_) {
      ZX_DEBUG_ASSERT(!callback_);
      pending_ = value;
      return true;
    }

    if (callback_) {
      ZX_DEBUG_ASSERT(!pending_);
      callback_(value);
      callback_ = nullptr;
      sent_ = value;
      return false;
    }

    pending_ = value;
    return false;
  }

 private:
  fit::function<void(T)> callback_;
  std::optional<T> pending_;
  std::optional<T> sent_;
  Compare eq_;
};

template <class T, class Compare>
class HangingGetHelper<T, Compare, false> {
 public:
  // Sets or updates the saved callback to the provided value. Returns true iff an existing callback
  // was overwritten.
  bool Get(fit::function<void(T)> callback) {
    if (callback_) {
      ZX_DEBUG_ASSERT(!pending_);
      callback_ = std::move(callback);
      return true;
    }

    if (pending_) {
      ZX_DEBUG_ASSERT(!callback_);
      callback(std::move(pending_.value()));
      pending_ = std::nullopt;
      return false;
    }

    callback_ = std::move(callback);
    return false;
  }

  // Sets or updates the pending value to the provided value. Returns true iff an existing value was
  // overwritten.
  bool Set(T value) {
    if (pending_) {
      ZX_DEBUG_ASSERT(!callback_);
      pending_ = std::move(value);
      return true;
    }

    if (callback_) {
      ZX_DEBUG_ASSERT(!pending_);
      callback_(std::move(value));
      callback_ = nullptr;
      return false;
    }

    pending_ = std::move(value);
    return false;
  }

 private:
  fit::function<void(T)> callback_;
  std::optional<T> pending_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_HANGING_GET_HELPER_HANGING_GET_HELPER_H_
