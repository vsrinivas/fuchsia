// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/clock.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>

#include <future>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/gtest/test_loop_fixture.h"

namespace cobalt {

using namespace testing;

class FuchsiaSystemClockTest : public ::gtest::TestLoopFixture {
 public:
  ~FuchsiaSystemClockTest() override = default;

 protected:
  void SetUp() override {
    EXPECT_EQ(ZX_OK, zx::clock::create(0, nullptr, &zircon_clock_));
    clock_.reset(new FuchsiaSystemClock(dispatcher(), zircon_clock_.borrow()));
  }

  void TearDown() override { clock_.reset(); }

  void SignalClockStarted() {
    zx::clock::update_args args;
    args.set_value(zx::time(3000));
    EXPECT_EQ(ZX_OK, zircon_clock_.update(args));
  }

  zx::clock zircon_clock_;
  std::unique_ptr<FuchsiaSystemClock> clock_;
};

TEST_F(FuchsiaSystemClockTest, AwaitExternalSourceInitiallyAccurate) {
  SignalClockStarted();

  bool called = false;
  clock_->AwaitExternalSource([&called]() { called = true; });
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(FuchsiaSystemClockTest, AwaitExternalSourceInitiallyInaccurate) {
  bool called = false;
  clock_->AwaitExternalSource([&called]() { called = true; });
  RunLoopUntilIdle();

  EXPECT_FALSE(called);

  SignalClockStarted();
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
}

TEST_F(FuchsiaSystemClockTest, NowBeforeInitialized) { EXPECT_EQ(clock_->now(), std::nullopt); }

TEST_F(FuchsiaSystemClockTest, NowAfterInitialized) {
  SignalClockStarted();

  clock_->AwaitExternalSource([]() {});

  RunLoopUntilIdle();

  EXPECT_NE(clock_->now(), std::nullopt);
}

// Tests our use of an atomic_bool. We can set |accurate_| true in
// one thread and read it as true in another thread.
TEST_F(FuchsiaSystemClockTest, NowFromAnotherThread) {
  SignalClockStarted();
  clock_->AwaitExternalSource([]() {});

  RunLoopUntilIdle();

  EXPECT_NE(std::async(std::launch::async, [this]() { return clock_->now(); }).get(), std::nullopt);
}

}  // namespace cobalt
