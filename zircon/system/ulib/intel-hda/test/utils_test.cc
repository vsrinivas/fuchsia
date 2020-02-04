// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda/utils/utils.h"

#include <lib/sync/completion.h>
#include <zircon/errors.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

class AutoAdvancingClock : public Clock {
 public:
  zx::time Now() { return now_; }
  void SleepUntil(zx::time time) { now_ = std::max(now_, time); }
  void AdvanceTime(zx::duration duration) { now_ = now_ + duration; }

 private:
  zx::time now_{};
};

TEST(WaitCondition, AlwaysTrue) {
  EXPECT_OK(WaitCondition(ZX_USEC(0), ZX_USEC(0), []() { return true; }));
}

TEST(WaitCondition, AlwaysFalse) {
  EXPECT_EQ(WaitCondition(ZX_USEC(0), ZX_USEC(0), []() { return false; }), ZX_ERR_TIMED_OUT);
}

TEST(WaitCondition, FrequentPolling) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // Poll every second for 10 seconds.
  WaitCondition(
      ZX_SEC(10), ZX_SEC(1),
      [&num_polls]() {
        num_polls++;
        return false;
      },
      &clock);

  // Ensure we polled 10 + 1 times.
  EXPECT_EQ(num_polls, 11);
}

TEST(WaitCondition, LongPollPeriod) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // Have a polling period far greater than the deadline.
  WaitCondition(
      ZX_SEC(1), ZX_SEC(100),
      [&num_polls]() {
        num_polls++;
        return false;
      },
      &clock);

  // Ensure we poll twice, once at the beginning and once at the end.
  EXPECT_EQ(num_polls, 2);
  EXPECT_EQ(clock.Now(), zx::time(0) + zx::sec(1));
}

TEST(WaitCondition, LongRunningCondition) {
  AutoAdvancingClock clock{};
  int num_polls = 0;

  // We want to poll every second for 10 seconds, but the condition takes
  // 100 seconds to evaluate.
  WaitCondition(
      ZX_SEC(10), ZX_SEC(1),
      [&num_polls, &clock]() {
        num_polls++;
        clock.AdvanceTime(zx::sec(100));
        return false;
      },
      &clock);

  // Ensure that we still polled twice.
  EXPECT_EQ(num_polls, 2);
}

}  // namespace
}  // namespace audio::intel_hda
