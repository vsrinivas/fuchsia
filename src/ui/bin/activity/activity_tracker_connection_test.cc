// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_tracker_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <memory>

#include "garnet/public/lib/gtest/test_loop_fixture.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace {

fuchsia::ui::activity::DiscreteActivity DiscreteActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::DiscreteActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

fuchsia::ui::activity::OngoingActivity OngoingActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::OngoingActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

}  // namespace

namespace activity {

class ActivityTrackerConnectionTest : public ::gtest::TestLoopFixture {
 public:
  ActivityTrackerConnectionTest() : driver_(dispatcher()) {}

  void SetUp() override {
    conn_ = std::make_unique<ActivityTrackerConnection>(&driver_, dispatcher(),
                                                        client_.NewRequest(dispatcher()));
    // Some tests rely on subtracting from Now(), so advance to a nonzero time
    RunLoopFor(zx::hour(1));
  }

 protected:
  StateMachineDriver driver_;
  std::unique_ptr<ActivityTrackerConnection> conn_;
  fuchsia::ui::activity::TrackerPtr client_;
};

TEST_F(ActivityTrackerConnectionTest, ReportActivity) {
  int callback_invocations = 0;
  auto callback = [&callback_invocations]() { callback_invocations++; };
  client_->ReportDiscreteActivity(DiscreteActivity(), Now().get(), std::move(callback));
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(callback_invocations, 1);
}

TEST_F(ActivityTrackerConnectionTest, ReportActivity_StaleEventIgnored) {
  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  // Send an event and then let the state machine driver time out (returning to IDLE).
  driver_.ReceiveDiscreteActivity(DiscreteActivity(), Now(), []() {});
  RunLoopUntilIdle();
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  int callback_invocations = 0;
  auto callback = [&callback_invocations]() { callback_invocations++; };
  auto old_time = Now() - zx::duration(zx::sec(5));
  client_->ReportDiscreteActivity(DiscreteActivity(), old_time.get(), std::move(callback));
  RunLoopUntilIdle();

  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
  // Make sure no channel errors were received and the callback was invoked
  EXPECT_FALSE(epitaph);
  EXPECT_EQ(callback_invocations, 1);
}

TEST_F(ActivityTrackerConnectionTest, ReportActivity_OutOfOrder) {
  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  int callback1_invocations = 0;
  auto callback1 = [&callback1_invocations]() { callback1_invocations++; };

  client_->ReportDiscreteActivity(DiscreteActivity(), Now().get(), std::move(callback1));
  RunLoopUntilIdle();

  int callback2_invocations = 0;
  auto callback2 = [&callback2_invocations]() { callback2_invocations++; };

  auto old_time = Now() - zx::duration(zx::sec(5));
  client_->ReportDiscreteActivity(DiscreteActivity(), old_time.get(), std::move(callback2));
  RunLoopUntilIdle();

  EXPECT_TRUE(epitaph);
  EXPECT_EQ(*epitaph, ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(callback1_invocations, 1);
  EXPECT_EQ(callback2_invocations, 0);
}

TEST_F(ActivityTrackerConnectionTest, StartStopOngoingActivity) {
  int start_callback_invocations = 0;
  auto start_callback = [&start_callback_invocations]() { start_callback_invocations++; };
  const OngoingActivityId activity_id = 1234;
  client_->StartOngoingActivity(activity_id, OngoingActivity(), Now().get(),
                                std::move(start_callback));
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(start_callback_invocations, 1);

  // Timeouts should not be processed
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);

  int end_callback_invocations = 0;
  auto end_callback = [&end_callback_invocations]() { end_callback_invocations++; };
  client_->EndOngoingActivity(activity_id, Now().get(), std::move(end_callback));
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(end_callback_invocations, 1);

  // The activity has ended so timeouts should now be respected
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityTrackerConnectionTest, StartOngoingActivity_StaleEventsIgnored) {
  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  // Send an event and then let the state machine driver time out (returning to IDLE).
  driver_.ReceiveDiscreteActivity(DiscreteActivity(), Now(), []() {});
  RunLoopUntilIdle();
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  int callback_invocations = 0;
  auto callback = [&callback_invocations]() { callback_invocations++; };
  auto old_time = Now() - zx::duration(zx::sec(5));
  OngoingActivityId id = 1234;
  client_->StartOngoingActivity(id, OngoingActivity(), old_time.get(), std::move(callback));
  RunLoopUntilIdle();

  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
  // Make sure no channel errors were received and the callback was invoked
  EXPECT_FALSE(epitaph);
  EXPECT_EQ(callback_invocations, 1);
}

TEST_F(ActivityTrackerConnectionTest, StartOngoingActivity_OutOfOrder) {
  // Send a discrete activity to bring the state machine to ACTIVE
  client_->ReportDiscreteActivity(DiscreteActivity(), Now().get(), []() {});
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);

  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  auto old_time = Now() - zx::duration(zx::sec(5));
  client_->StartOngoingActivity(1234, OngoingActivity(), old_time.get(),
                                []() { ASSERT_FALSE("Callback was unexpectedly invoked"); });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  ASSERT_TRUE(epitaph);
  EXPECT_EQ(*epitaph, ZX_ERR_OUT_OF_RANGE);

  // Timeouts should still be processed (no ongoing activity should have started)
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityTrackerConnectionTest, CleansUpOngoingActivitiesOnStop) {
  int callback_invocations = 0;
  client_->StartOngoingActivity(1234, OngoingActivity(), Now().get(),
                                [&callback_invocations]() { callback_invocations = true; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(callback_invocations, 1);

  conn_->Stop();
  RunLoopUntilIdle();

  // Timeouts will now be processed since the activity was cleaned up
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityTrackerConnectionTest, CleansUpOngoingActivitiesOnDestruction) {
  int callback_invocations = 0;
  client_->StartOngoingActivity(1234, OngoingActivity(), Now().get(),
                                [&callback_invocations]() { callback_invocations++; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(callback_invocations, 1);

  conn_.reset();
  RunLoopUntilIdle();

  // Timeouts will now be processed since the activity was cleaned up
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::IDLE);
}

}  // namespace activity
