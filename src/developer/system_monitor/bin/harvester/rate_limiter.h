// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_RATE_LIMITER_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_RATE_LIMITER_H_

#include <functional>
#include <stddef.h>

// Only runs the supplied function every Nth time this is called.
class RateLimiter {
 public:
  explicit RateLimiter(size_t refresh_interval)
      : refresh_interval_(refresh_interval) {}

  void Run(const std::function<void()>& maybeCallback) {
    if (counter_ % refresh_interval_ == 0) {
      maybeCallback();
    }

    ++counter_;
  }

 private:
  const size_t refresh_interval_;
  int counter_ = 0;
};

#endif // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_RATE_LIMITER_H_

