// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/activity_service/activity_service_tracker_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <memory>

#include "garnet/bin/ui/activity_service/state_machine_driver.h"
#include "garnet/public/lib/gtest/test_loop_fixture.h"

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

namespace activity_service {

class ActivityServiceTrackerConnectionTest : public ::gtest::TestLoopFixture {
 public:
  ActivityServiceTrackerConnectionTest() : driver_(dispatcher()) {}

  void SetUp() override {
    conn_ = std::make_unique<ActivityServiceTrackerConnection>(
        &driver_, dispatcher(), client_.NewRequest(dispatcher()),
        testing::UnitTest::GetInstance()->random_seed());
  }

 private:
  StateMachineDriver driver_;
  std::unique_ptr<ActivityServiceTrackerConnection> conn_;
  fuchsia::ui::activity::TrackerPtr client_;
};

TEST_F(ActivityServiceTrackerConnectionTest, ReportActivity) {
  client_->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(ActivityServiceTrackerConnectionTest, ReportActivity_Failed) {
  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  auto old_time = Now() - zx::duration(zx::sec(5));
  client_->ReportDiscreteActivity(DiscreteActivity(), old_time.get());
  RunLoopUntilIdle();

  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::IDLE);
  EXPECT_TRUE(epitaph);
  EXPECT_EQ(*epitaph, ZX_ERR_OUT_OF_RANGE);
}

TEST_F(ActivityServiceTrackerConnectionTest, StartStopOngoingActivity) {
  std::optional<OngoingActivityId> activity_id;
  client_->StartOngoingActivity(OngoingActivity(), Now().get(),
                                [&activity_id](OngoingActivityId id) { activity_id = id; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);
  ASSERT_TRUE(activity_id);

  // Timeouts should not be processed
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);

  client_->EndOngoingActivity(*activity_id, Now().get());
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);

  // The activity has ended so timeouts should now be respected
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityServiceTrackerConnectionTest, StartOngoingActivity_Failed) {
  // Send a discrete activity to bring the state machine to ACTIVE
  client_->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);

  std::optional<zx_status_t> epitaph;
  client_.set_error_handler([&epitaph](zx_status_t status) { epitaph = status; });

  auto old_time = Now() - zx::duration(zx::sec(5));
  client_->StartOngoingActivity(
      OngoingActivity(), old_time.get(),
      [](__UNUSED OngoingActivityId id) { ASSERT_FALSE("Callback was unexpectedly invoked"); });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);
  ASSERT_TRUE(epitaph);
  EXPECT_EQ(*epitaph, ZX_ERR_OUT_OF_RANGE);

  // Timeouts should still be processed (no ongoing activity should have started)
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityServiceTrackerConnectionTest, CleansUpOngoingActivitiesOnStop) {
  bool callback_invoked = false;
  client_->StartOngoingActivity(
      OngoingActivity(), Now().get(),
      [&callback_invoked](__UNUSED OngoingActivityId id) { callback_invoked = true; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_TRUE(callback_invoked);

  conn_->Stop();
  RunLoopUntilIdle();

  // Timeouts will now be processed since the activity was cleaned up
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityServiceTrackerConnectionTest, CleansUpOngoingActivitiesOnDestruction) {
  bool callback_invoked = false;
  client_->StartOngoingActivity(
      OngoingActivity(), Now().get(),
      [&callback_invoked](__UNUSED OngoingActivityId id) { callback_invoked = true; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);
  EXPECT_TRUE(callback_invoked);

  conn_.reset();
  RunLoopUntilIdle();

  // Timeouts will now be processed since the activity was cleaned up
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::IDLE);
}

}  // namespace activity_service
