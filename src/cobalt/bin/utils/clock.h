// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_CLOCK_H_
#define SRC_COBALT_BIN_UTILS_CLOCK_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <chrono>
#include <optional>

#include "fuchsia/time/cpp/fidl.h"
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

class FuchsiaSystemClock : public FuchsiaSystemClockInterface {
 public:
  explicit FuchsiaSystemClock(const std::shared_ptr<sys::ServiceDirectory>& service_directory);

  // Returns the current time once the Fuchsia timekeeper service reports that
  // the system clock has been initialized from an external source.
  std::optional<std::chrono::system_clock::time_point> now() override;

  // Wait for the system clock to become accurate, then call the callback.
  //
  // Uses the timekeeper (fuchsia.time.Utc) service to wait for the clock to be
  // initialized from a suitably accurate external time source.
  void AwaitExternalSource(std::function<void()> callback) override;

 private:
  void WatchStateCallback(const fuchsia::time::UtcState& utc_state);

  fuchsia::time::UtcPtr utc_;
  std::function<void()> callback_;
  bool accurate_ = false;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_CLOCK_H_
