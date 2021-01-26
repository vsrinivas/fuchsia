// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

class FuchsiaClockTest : public gtest::TestLoopFixture {
 public:
  FuchsiaClockTest() {
    zx_clock_create_args_v1_t clock_args{.backstop_time = 0};
    FX_CHECK(zx::clock::create(ZX_CLOCK_ARGS_VERSION(1), &clock_args,
                               &clock_handle_) == ZX_OK);
  }

 protected:
  zx::clock clock_handle_;
};

TEST_F(FuchsiaClockTest, BeforeSignalOptionalsAreEmpty) {
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();

  harvester::FuchsiaClock clock(dispatcher(), std::move(test_clock),
                                zx::unowned_clock(clock_handle_.get_handle()));

  EXPECT_EQ(clock.now(), std::nullopt);
  EXPECT_EQ(clock.nanoseconds(), std::nullopt);
}

TEST_F(FuchsiaClockTest, BeforeWaitForStartOptionalsAreEmpty) {
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();

  constexpr zx::time start_time(
      (zx::hour(9) + zx::min(31) + zx::sec(42)).get());

  test_clock->Set(start_time);

  harvester::FuchsiaClock clock(dispatcher(), std::move(test_clock),
                                zx::unowned_clock(clock_handle_.get_handle()));

  clock_handle_.update(zx::clock::update_args{}.set_value(start_time));

  EXPECT_EQ(clock.now(), std::nullopt);
  EXPECT_EQ(clock.nanoseconds(), std::nullopt);
}

TEST_F(FuchsiaClockTest, AfterWaitForStartOptionalsAreStillEmptyIfNotStarted) {
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();

  harvester::FuchsiaClock clock(dispatcher(), std::move(test_clock),
                                zx::unowned_clock(clock_handle_.get_handle()));

  clock.WaitForStart([](zx_status_t status) {});

  RunLoopUntilIdle();

  EXPECT_EQ(clock.now(), std::nullopt);
  EXPECT_EQ(clock.nanoseconds(), std::nullopt);
}

TEST_F(FuchsiaClockTest, AfterWaitForStartHasValuesIfAlreadyStarted) {
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();

  constexpr zx::time start_time(
      (zx::hour(9) + zx::min(31) + zx::sec(42)).get());

  test_clock->Set(start_time);

  harvester::FuchsiaClock clock(dispatcher(), std::move(test_clock),
                                zx::unowned_clock(clock_handle_.get_handle()));

  clock_handle_.update(zx::clock::update_args().set_value(start_time));

  bool isCalled = false;

  clock.WaitForStart([&isCalled](zx_status_t status) {
    isCalled = true;
    EXPECT_EQ(status, ZX_OK);
  });

  EXPECT_TRUE(isCalled);
  EXPECT_TRUE(clock.now().has_value());
  EXPECT_TRUE(clock.nanoseconds().has_value());
}

TEST_F(FuchsiaClockTest, WaitWorksAsyncAndMakesClockAvailable) {
  std::unique_ptr<timekeeper::TestClock> test_clock =
      std::make_unique<timekeeper::TestClock>();

  constexpr zx::time start_time(
      (zx::hour(9) + zx::min(31) + zx::sec(42)).get());

  test_clock->Set(start_time);

  harvester::FuchsiaClock clock(dispatcher(), std::move(test_clock),
                                zx::unowned_clock(clock_handle_.get_handle()));

  bool isCalled = false;

  clock.WaitForStart([&isCalled](zx_status_t status) {
    isCalled = true;
    EXPECT_EQ(status, ZX_OK);
  });

  RunLoopUntilIdle();

  EXPECT_FALSE(isCalled);
  EXPECT_FALSE(clock.now().has_value());
  EXPECT_FALSE(clock.nanoseconds().has_value());

  EXPECT_EQ(
      clock_handle_.update(zx::clock::update_args().set_value(start_time)),
      ZX_OK);

  RunLoopUntilIdle();

  EXPECT_TRUE(isCalled);
  EXPECT_TRUE(clock.now().has_value());
  EXPECT_TRUE(clock.nanoseconds().has_value());
}
