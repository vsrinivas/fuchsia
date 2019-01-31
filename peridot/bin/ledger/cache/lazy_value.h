// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CACHE_LAZY_VALUE_H_
#define PERIDOT_BIN_LEDGER_CACHE_LAZY_VALUE_H_

#include <functional>
#include <vector>

#include <lib/fit/function.h>

namespace cache {
// Implements a self-populating lazy value.
//
// This class allows to setup a lazy value that will be generated asynchronously
// the first time it is requested.
//
// V is the type of the lazy value. V must be default-constructible and either
//   copyable or movable.
// S is the type of the success status for the data generator.
template <typename V, typename S>
class LazyValue {
 public:
  // Constructor.
  //
  // |ok_status| is the success status of the generator.
  // |generator| generates the value. It takes a callback to returns its result.
  //   It must return |ok_status| as a status when the request is successful.
  //   Any other return value is considered a failure.
  LazyValue(S ok_status,
            fit::function<void(fit::function<void(S, V)>)> generator)
      : ok_status_(ok_status),
        generator_(std::move(generator)),
        value_set_(false) {}

  // Retrieves the value and returns it to |callback|.
  //
  // If the value is cached, |callback| will be called synchronously. Otherwise,
  // |generator| will be called, and depending on its implementation, |callback|
  // might be called synchronously or not.
  void Get(fit::function<void(S, const V&)> callback) {
    if (value_set_) {
      callback(ok_status_, value_);
      return;
    }
    requests_.push_back(std::move(callback));
    if (requests_.size() == 1) {
      generator_([this](S status, V value) {
        auto callbacks = std::move(requests_);
        requests_.clear();

        if (status == ok_status_) {
          value_ = std::move(value);
          value_set_ = true;
        }
        for (const auto& callback : callbacks) {
          callback(status, value_);
        }
      });
    }
  }

 private:
  S ok_status_;
  fit::function<void(fit::function<void(S, V)>)> generator_;
  V value_;
  bool value_set_;
  std::vector<fit::function<void(S, const V&)>> requests_;
};

}  // namespace cache

#endif  // PERIDOT_BIN_LEDGER_CACHE_LAZY_VALUE_H_
