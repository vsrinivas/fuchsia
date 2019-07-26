// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTING_FAKE_CLOCK_H_
#define GARNET_BIN_COBALT_TESTING_FAKE_CLOCK_H_

#include <chrono>

#include "src/cobalt/bin/utils/clock.h"

namespace cobalt {

// An implementation of SteadyClock that returns a time that does not
// increase with real time but only when Increment() is invoked. For use in
// tests.
class FakeSteadyClock : public SteadyClock {
 public:
  std::chrono::steady_clock::time_point Now() override { return now_; }

  void Increment(std::chrono::seconds increment_seconds) { now_ += increment_seconds; }

  void set_time(std::chrono::steady_clock::time_point t) { now_ = t; }

 private:
  std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now();
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTING_FAKE_CLOCK_H_
