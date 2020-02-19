// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <fbl/string_printf.h>
#include <lib/zx/timer.h>
#include <perftest/perftest.h>

namespace {

struct TimerState {
  zx::duration wait_time;
  zx::duration slack_time;
};

const char* SlackTypeToString(uint32_t slack_type) {
  switch (slack_type) {
    case ZX_TIMER_SLACK_LATE:
      return "SlackLate";
    case ZX_TIMER_SLACK_EARLY:
      return "SlackEarly";
    case ZX_TIMER_SLACK_CENTER:
      return "SlackCenter";
    default:
      ZX_ASSERT_MSG(true, "Slack type unsupported\n");
      return nullptr;
  }
}

// Measures how long a timer takes to fire based on the wait time, slack time,
// and slack type. This can be useful for measuring the overhead of sleeping.
// It can also be used to measure the variation in actual sleep times.
bool TimerWaitTest(perftest::RepeatState* state, TimerState timer_state, uint32_t slack_type) {
  zx_status_t status;
  zx::timer timer;

  status = zx::timer::create(slack_type, ZX_CLOCK_MONOTONIC, &timer);
  ZX_ASSERT(status == ZX_OK);

  while (state->KeepRunning()) {
    status = timer.set(zx::deadline_after(timer_state.wait_time), timer_state.slack_time);
    ZX_ASSERT(status == ZX_OK);
    zx_status_t status = timer.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), nullptr);
    ZX_ASSERT(status == ZX_OK);
  }

  return true;
}

void RegisterTests() {
  const TimerState timers[] = {TimerState{zx::msec(1), zx::usec(0)},
                               TimerState{zx::msec(1), zx::usec(500)}};
  const uint32_t slack_types[] = {ZX_TIMER_SLACK_LATE, ZX_TIMER_SLACK_EARLY, ZX_TIMER_SLACK_CENTER};

  for (auto timer : timers) {
    for (auto slack_type : slack_types) {
      auto name = fbl::StringPrintf("Timer/%lumsWait/%s%luus", timer.wait_time.to_msecs(),
                                    SlackTypeToString(slack_type), timer.slack_time.to_usecs());
      perftest::RegisterTest(name.c_str(), TimerWaitTest, timer, slack_type);
    }
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
