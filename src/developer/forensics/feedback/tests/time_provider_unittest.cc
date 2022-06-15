// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/time_provider.h"

#include <memory>

#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/testing/loop_fixture/test_loop.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics::feedback {
namespace {

constexpr timekeeper::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());
constexpr char kTimeStr[] = "1970-01-01 07:14:52 GMT";

class TimeProviderTest : public UnitTestFixture {
 public:
  TimeProviderTest() {
    zx_clock_create_args_v1_t clock_args{.backstop_time = 0};
    FX_CHECK(zx::clock::create(0u, &clock_args, &clock_handle_) == ZX_OK);
  }

 protected:
  void StartClock() {
    if (const zx_status_t status =
            clock_handle_.update(zx::clock::update_args().set_value(zx::time(kTime.get())));
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "Failed to start clock";
    }
  }

  void SetUpTimeProvider() {
    auto clock = std::make_unique<timekeeper::TestClock>();
    clock->Set(kTime);

    time_provider_ = std::make_unique<TimeProvider>(
        dispatcher(), zx::unowned_clock(clock_handle_.get_handle()), std::move(clock));
  }

  zx::clock clock_handle_;
  std::unique_ptr<TimeProvider> time_provider_;
};

TEST_F(TimeProviderTest, Check_ClockStarts) {
  SetUpTimeProvider();
  EXPECT_FALSE(time_provider_->Get().at(kDeviceUtcTimeKey).HasValue());

  StartClock();
  RunLoopUntilIdle();

  ASSERT_TRUE(time_provider_->Get().at(kDeviceUtcTimeKey).HasValue());
  EXPECT_EQ(time_provider_->Get().at(kDeviceUtcTimeKey).Value(), kTimeStr);
}

TEST_F(TimeProviderTest, Check_ClockStartsBeforeFeedback) {
  StartClock();
  SetUpTimeProvider();
  RunLoopUntilIdle();

  ASSERT_TRUE(time_provider_->Get().at(kDeviceUtcTimeKey).HasValue());
  EXPECT_EQ(time_provider_->Get().at(kDeviceUtcTimeKey).Value(), kTimeStr);
}

}  // namespace
}  // namespace forensics::feedback
