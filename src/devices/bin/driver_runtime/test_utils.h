// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_TEST_UTILS_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_TEST_UTILS_H_

#include <lib/fit/function.h>

#include <thread>

namespace test_utils {

// RAII for joining threads on destruction.
// Useful when an assertion fails.
class AutoJoinThread {
 public:
  explicit AutoJoinThread(fit::function<void()> fn) {
    thread_ = std::thread(std::move(fn));
    valid_ = true;
  }

  template <typename Fn, typename... Args>
  explicit AutoJoinThread(Fn fn, Args&&... args) {
    thread_ = std::thread(fn, std::forward<Args>(args)...);
    valid_ = true;
  }

  AutoJoinThread(AutoJoinThread&& other) noexcept {
    thread_.swap(other.thread_);
    valid_ = other.valid_;
    other.valid_ = false;
  }

  void Join() {
    if (valid_) {
      valid_ = false;
      thread_.join();
    }
  }

  ~AutoJoinThread() {
    if (valid_) {
      thread_.join();
      valid_ = false;
    }
  }

 private:
  std::thread thread_;
  bool valid_ = false;
};

}  // namespace test_utils

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_TEST_UTILS_H_
