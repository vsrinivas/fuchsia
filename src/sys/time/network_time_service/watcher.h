// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_
#define SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/comparison.h>
#include <lib/fit/function.h>

namespace network_time_service {

// A hanging get handler that allows parking callbacks, then invoking them
// later when a new value is available. This class is not thread safe.
template <class T>
class Watcher {
 public:
  using Callback = fit::function<void(T)>;

  Watcher() {}

  Watcher(T initial_value) : current_(std::move(initial_value)) {}

  // Register a callback that is executed when a new value is produced and
  // return if successful. Returns false without registering the callback if
  // another callback is already registered.
  bool Watch(Callback callback) {
    if (!callback_) {
      callback_ = std::move(callback);
      CallbackIfNeeded();
      return true;
    }
    return false;
  }

  // Push a new value. Any registered callback is invoked if the value has changed.
  void Update(T new_value) {
    current_ = std::move(new_value);
    CallbackIfNeeded();
  }

  // Clears any registered callback and last sent state.
  void ResetClient() {
    last_sent_.reset();
    callback_.reset();
  }

 private:
  T CloneValue(const T& sample) {
    T clone;
    fidl::Clone(sample, &clone);
    return clone;
  }

  void CallbackIfNeeded() {
    if (!callback_) {
      return;
    }
    if (current_ && (!last_sent_ || !fidl::Equals(*current_, *last_sent_))) {
      callback_.value()(CloneValue(current_.value()));
      callback_.reset();
      last_sent_ = CloneValue(current_.value());
    }
  }

  std::optional<Callback> callback_;
  std::optional<T> last_sent_;
  std::optional<T> current_;
};

}  // namespace network_time_service

#endif  // SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_
