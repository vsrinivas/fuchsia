// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_UTILS_CLOCK_H_
#define GARNET_BIN_COBALT_UTILS_CLOCK_H_

#include <chrono>

namespace cobalt {

// An abstract interface to a SteadyClock that may be faked in tests.
class SteadyClock {
 public:
  virtual ~SteadyClock() = default;

  virtual std::chrono::steady_clock::time_point Now() = 0;
};

// An implementation of SteadyClock that uses a real clock.
class RealSteadyClock : public SteadyClock {
 public:
  std::chrono::steady_clock::time_point Now() override {
    return std::chrono::steady_clock::now();
  }
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_UTILS_CLOCK_H_
