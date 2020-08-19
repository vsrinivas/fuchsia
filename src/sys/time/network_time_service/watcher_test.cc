// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/network_time_service/watcher.h"

#include <fuchsia/time/external/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

using namespace fuchsia::time::external;
namespace network_time_service {

// Tests Watcher using TimeSample as the contained value.
class SampleWatcherTest : public gtest::TestLoopFixture {};

using SampleWatcher = Watcher<TimeSample>;

TEST_F(SampleWatcherTest, FirstWatch) {
  SampleWatcher watcher;
  bool initial_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 20);
    EXPECT_EQ(sample.utc(), 40);
    initial_called = true;
  }));
  EXPECT_FALSE(initial_called);
  TimeSample initial_sample;
  initial_sample.set_monotonic(20);
  initial_sample.set_utc(40);
  watcher.Update(std::move(initial_sample));
  EXPECT_TRUE(initial_called);
}

TEST_F(SampleWatcherTest, FirstWatchWithInitial) {
  TimeSample initial_sample;
  initial_sample.set_monotonic(20);
  initial_sample.set_utc(40);
  SampleWatcher watcher(std::move(initial_sample));

  bool initial_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 20);
    EXPECT_EQ(sample.utc(), 40);
    initial_called = true;
  }));
  EXPECT_TRUE(initial_called);
}

TEST_F(SampleWatcherTest, WatchAfterUpdate) {
  SampleWatcher watcher;
  TimeSample sample;
  sample.set_monotonic(20);
  sample.set_utc(40);
  watcher.Update(std::move(sample));

  bool callback_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 20);
    EXPECT_EQ(sample.utc(), 40);
    callback_called = true;
  }));
  EXPECT_TRUE(callback_called);
}

TEST_F(SampleWatcherTest, RegisterMultipleCallbacks) {
  SampleWatcher watcher;
  bool first_watch_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) { first_watch_called = true; }));
  EXPECT_FALSE(watcher.Watch([&](TimeSample sample) { FAIL(); }));

  watcher.Update(TimeSample());
  EXPECT_TRUE(first_watch_called);
}

TEST_F(SampleWatcherTest, WatchMultiple) {
  SampleWatcher watcher;
  bool initial_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 20);
    EXPECT_EQ(sample.utc(), 40);
    initial_called = true;
  }));
  EXPECT_FALSE(initial_called);
  TimeSample initial_sample;
  initial_sample.set_monotonic(20);
  initial_sample.set_utc(40);
  watcher.Update(std::move(initial_sample));
  EXPECT_TRUE(initial_called);

  // second call returns only after update pushed
  bool second_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 30);
    EXPECT_EQ(sample.utc(), 60);
    second_called = true;
  }));
  EXPECT_FALSE(second_called);
  TimeSample second_sample;
  second_sample.set_monotonic(30);
  second_sample.set_utc(60);
  watcher.Update(std::move(second_sample));
  EXPECT_TRUE(second_called);
}

TEST_F(SampleWatcherTest, MultipleUpdates) {
  SampleWatcher watcher;
  TimeSample initial_sample;
  initial_sample.set_monotonic(20);
  initial_sample.set_utc(40);
  watcher.Update(std::move(initial_sample));
  bool initial_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 20);
    EXPECT_EQ(sample.utc(), 40);
    initial_called = true;
  }));
  EXPECT_TRUE(initial_called);

  // second sample triggers callback
  TimeSample second_sample;
  second_sample.set_monotonic(30);
  second_sample.set_utc(60);
  watcher.Update(std::move(second_sample));
  bool second_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 30);
    EXPECT_EQ(sample.utc(), 60);
    second_called = true;
  }));
  EXPECT_TRUE(second_called);

  // identical sample does not trigger callback
  TimeSample third_sample;
  third_sample.set_monotonic(30);
  third_sample.set_utc(60);
  watcher.Update(std::move(third_sample));
  watcher.Watch([&](TimeSample sample) { FAIL(); });
}

TEST_F(SampleWatcherTest, ResetClient) {
  SampleWatcher watcher;
  TimeSample sample;
  sample.set_monotonic(70);
  sample.set_utc(140);
  watcher.Update(std::move(sample));
  watcher.Watch([&](TimeSample sample) {});
  // second watch should not return as there's no update
  watcher.Watch([&](TimeSample sample) { FAIL(); });
  watcher.ResetClient();
  bool third_called = false;
  EXPECT_TRUE(watcher.Watch([&](TimeSample sample) {
    EXPECT_EQ(sample.monotonic(), 70);
    EXPECT_EQ(sample.utc(), 140);
    third_called = true;
  }));
  EXPECT_TRUE(third_called);
}

}  // namespace network_time_service
