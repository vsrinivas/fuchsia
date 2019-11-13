// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ABS_CLOCK_CLOCK_H_
#define ABS_CLOCK_CLOCK_H_

#include <lib/zx/time.h>
#include <zircon/time.h>

#include <memory>

namespace abs_clock {

struct FakeClockState;

// An abstract clock interface.
//
// This simplifies testing code that needs access to time. Code should
// accept an abstract |Clock *| and use the provided methods.
//
// In a production, the the code should be passed an instance of
// RealClock, which uses the standard kernel-provided time mechanisms.
//
// Test code, however, can pass in a FakeClock, which allows test code
// to take control over time as required.
class Clock {
 public:
  virtual ~Clock() = default;

  // Return the current time.
  virtual zx::time Now() = 0;

  // Sleep until the given deadline.
  virtual void SleepUntil(zx::time deadline) = 0;
};

// A real implementation of a clock.
//
// Call "RealClock::Get()" to get a shared, global instance of the
// clock.
class RealClock final : public Clock {
 public:
  zx::time Now() override { return zx::time(zx_clock_get_monotonic()); }

  void SleepUntil(zx::time deadline) override { zx::nanosleep(deadline); }

  static RealClock* Get() {
    static RealClock global_clock{};
    return &global_clock;
  }

 private:
  RealClock() = default;
};

class FakeClock : public Clock {
 public:
  // Create a FakeClock.
  FakeClock();
  FakeClock(zx::time start_time);
  ~FakeClock();

  // Advance the time by the given duration.
  void AdvanceTime(zx::duration duration);

  // Clock implementation.
  zx::time Now() override;
  void SleepUntil(zx::time deadline) override;

 private:
  // State of the FakeClock.
  //
  // We use an opaque state pointer here to avoid exposing header
  // dependencies of FakeClock on our callers.
  const std::unique_ptr<FakeClockState> state_;
};

}  // namespace abs_clock

#endif  // ABS_CLOCK_CLOCK_H_
