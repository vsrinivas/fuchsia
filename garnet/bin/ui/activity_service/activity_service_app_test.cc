// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/activity_service/activity_service_app.h"

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

class FakeListener : public fuchsia::ui::activity::Listener {
 public:
  FakeListener() = default;

  virtual void OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                              OnStateChangedCallback callback) {
    state_changes_.emplace_back(state, transition_time);
    callback();
  }

  struct StateChange {
    StateChange(fuchsia::ui::activity::State state, zx_time_t time) : state(state), time(time) {}
    fuchsia::ui::activity::State state;
    zx::time time;
  };
  const std::vector<StateChange>& StateChanges() const { return state_changes_; }

 private:
  std::vector<StateChange> state_changes_;
};

}  // namespace

namespace activity_service {

class ActivityServiceAppTest : public ::gtest::TestLoopFixture {
 public:
  ActivityServiceAppTest() = default;

  void SetUp() override {
    auto driver = std::make_unique<StateMachineDriver>(dispatcher());
    driver_ = driver.get();
    app_ = std::make_unique<ActivityServiceApp>(std::move(driver), dispatcher());
  }

 protected:
  std::unique_ptr<ActivityServiceApp> app_;
  const StateMachineDriver* driver_;
};

namespace {

TEST_F(ActivityServiceAppTest, Tracker_ConnectDisconnect) {
  {
    fuchsia::ui::activity::TrackerPtr tracker;
    app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->tracker_bindings().size(), 0u);
}

TEST_F(ActivityServiceAppTest, Tracker_Multiple_ConnectDisconnect) {
  {
    fuchsia::ui::activity::TrackerPtr tracker1, tracker2;
    app_->AddTrackerBinding(tracker1.NewRequest(dispatcher()));
    app_->AddTrackerBinding(tracker2.NewRequest(dispatcher()));
    RunLoopUntilIdle();
    EXPECT_EQ(app_->tracker_bindings().size(), 2u);
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->tracker_bindings().size(), 0u);
}

TEST_F(ActivityServiceAppTest, Tracker_SendActivity) {
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));

  ASSERT_EQ(driver_->state(), fuchsia::ui::activity::State::IDLE);
  tracker->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  RunLoopUntilIdle();
  EXPECT_EQ(driver_->state(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(ActivityServiceAppTest, Tracker_OngoingActivity) {
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));
  ASSERT_EQ(driver_->state(), fuchsia::ui::activity::State::IDLE);
  OngoingActivityId id;

  tracker->StartOngoingActivity(OngoingActivity(), Now().get(),
                                [&id](OngoingActivityId returned_id) { id = returned_id; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  // No state change expected after timeout since there is an ongoing activity
  EXPECT_EQ(driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  tracker->EndOngoingActivity(id, Now().get());
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_->state(), fuchsia::ui::activity::State::IDLE);
}

}  // namespace

}  // namespace activity_service
