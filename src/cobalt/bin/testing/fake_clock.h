// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTING_FAKE_CLOCK_H_
#define SRC_COBALT_BIN_TESTING_FAKE_CLOCK_H_

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <optional>

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

// An implementation of FuchsiaSystemClockInterface that starts with an
// inaccurate time, and declares itself accurate on the first call to
// AwaitExternalSource.
class FakeFuchsiaSystemClock : public FuchsiaSystemClockInterface {
 public:
  explicit FakeFuchsiaSystemClock(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  std::optional<std::chrono::system_clock::time_point> now() override {
    if (accurate_) {
      return std::chrono::system_clock::now();
    }
    return std::nullopt;
  }

  void AwaitExternalSource(std::function<void()> callback) override {
    // CobaltApp relies on the callback to be run asynchronously as the callback it provides
    // contains references to pointers that it initializes later in its constructor.
    await_external_task_.set_handler(
        [this, callback = std::move(callback)](async_dispatcher_t* dispatcher, async::Task* wait,
                                               zx_status_t status) {
          if (status == ZX_ERR_CANCELED) {
            FX_LOGS(ERROR) << "Failed to wait for clock initiialization";
            return;
          }

          accurate_ = true;
          callback();
        });
    await_external_task_.Post(dispatcher_);
  }

 private:
  std::atomic_bool accurate_ = false;
  async_dispatcher_t* dispatcher_;  // not owned.
  async::Task await_external_task_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTING_FAKE_CLOCK_H_
