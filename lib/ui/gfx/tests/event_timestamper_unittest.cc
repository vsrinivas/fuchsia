// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <chrono>
#include <thread>

#include "garnet/lib/ui/gfx/tests/util.h"
#include "garnet/lib/ui/gfx/util/event_timestamper.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic {
namespace gfx {
namespace test {

using EventTimestamperTest = ::gtest::TestLoopFixture;

TEST_F(EventTimestamperTest, DISABLED_SmokeTest) {
  constexpr size_t kEventCount = 3;
  std::vector<zx::event> events;
  std::vector<EventTimestamper::Watch> watches;
  std::vector<zx_time_t> target_callback_times;

  events.resize(kEventCount);
  watches.reserve(kEventCount);
  target_callback_times.resize(kEventCount);

  auto timestamper = std::make_unique<EventTimestamper>();

  for (size_t i = 0; i < kEventCount; ++i) {
    ASSERT_EQ(ZX_OK, zx::event::create(0u, &(events[i])));
    watches.emplace_back(timestamper.get(), CopyEvent(events[i]),
                         ZX_EVENT_SIGNALED, [&, i = i](zx_time_t timestamp) {
                           ASSERT_GT(kEventCount, i);
                           EXPECT_GT(target_callback_times[i], 0u);
                           EXPECT_LE(target_callback_times[i], timestamp);
                           target_callback_times[i] = 0;
                         });
    target_callback_times[i] = 0;
  }

  for (size_t i = 0; i < kEventCount; ++i) {
    target_callback_times[i] = zx_clock_get(ZX_CLOCK_MONOTONIC);
    events[i].signal(0u, ZX_EVENT_SIGNALED);
    watches[i].Start();
  }

  RunLoopUntilIdle();
  for (size_t i = 0; i < kEventCount; ++i) {
    EXPECT_EQ(0u, target_callback_times[i]);
  }

  // Watches must not outlive the timestamper.
  watches.clear();
  timestamper.reset();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
