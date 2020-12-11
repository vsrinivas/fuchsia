// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_CLOCK_H_
#define SRC_COBALT_BIN_UTILS_CLOCK_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <chrono>
#include <optional>

#include "third_party/cobalt/src/lib/util/clock.h"

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
  std::chrono::steady_clock::time_point Now() override { return std::chrono::steady_clock::now(); }
};

class FuchsiaSystemClockInterface : public util::ValidatedClockInterface {
 public:
  // Waits for the system clock to become accurate, then call the callback.
  virtual void AwaitExternalSource(std::function<void()> callback) = 0;
};

// An implementation of FuchsiaSystemClockInterface that uses the UTC clock installed
// in the runtime.
class FuchsiaSystemClock : public FuchsiaSystemClockInterface {
 public:
  // Construct a |FuchsiaSystemClock| that reads the UTC clock passed to the runtime.
  explicit FuchsiaSystemClock(async_dispatcher_t* dispatcher);
  // Construct a |FuchsiaSystemClock| that uses the given |clock| to check if the clock is
  // started, but reads time off of the UTC clock passed to the runtime. This constructor is
  // only intended for testing this class.
  explicit FuchsiaSystemClock(async_dispatcher_t* dispatcher, zx::unowned_clock clock);

  // Returns the current time once the Fuchsia timekeeper service reports that
  // the system clock has been initialized from an external source.
  std::optional<std::chrono::system_clock::time_point> now() override;

  // Wait for the system clock to become accurate, then call the callback.
  //
  // Uses a clock handle to wait for the ZX_CLOCK_STARTED signal to be asserted.
  void AwaitExternalSource(std::function<void()> callback) override;

 private:
  std::atomic_bool accurate_ = false;
  // Async dispatcher used for watching clock signals.
  async_dispatcher_t* dispatcher_;  // not owned.
  async::WaitOnce utc_start_wait_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_CLOCK_H_
