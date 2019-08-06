#include "garnet/bin/ui/activity_service/state_machine_driver.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <iostream>
#include <memory>
#include <optional>

#include "garnet/bin/ui/activity_service/activity_state_machine.h"
#include "lib/gtest/test_loop_fixture.h"

namespace {

fuchsia::ui::activity::DiscreteActivity DiscreteActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::DiscreteActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

constexpr activity_service::OngoingActivityId kActivityId = 1234u;

}  // namespace

namespace activity_service {

class StateMachineDriverTest : public ::gtest::TestLoopFixture {
 public:
  StateMachineDriverTest() = default;
  ~StateMachineDriverTest() override = default;

  void SetUp() override {
    TestLoopFixture::SetUp();
    state_machine_driver_ = std::make_unique<StateMachineDriver>(dispatcher());
  }

 protected:
  std::unique_ptr<StateMachineDriver> state_machine_driver_;
};

TEST_F(StateMachineDriverTest, StartsInIdleState) {
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(StateMachineDriverTest, IgnoresEventsBeforeDriverInitTime) {
  auto t_past = Now() - zx::duration(zx::sec(1));

  // Any events at time t_past (which is < Now) should be ignored
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), t_past),
            ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(state_machine_driver_->StartOngoingActivity(kActivityId, t_past), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(state_machine_driver_->EndOngoingActivity(kActivityId, t_past), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(StateMachineDriverTest, IgnoresOldEvents) {
  auto t_present = Now() + zx::duration(zx::sec(1));
  auto t_past = Now();
  // Advances time to t_present
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), t_present), ZX_OK);
  RunLoopUntil(t_present);

  // Any events at time t_past (which is < t_present) should be ignored
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), t_past),
            ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(state_machine_driver_->StartOngoingActivity(kActivityId, t_past), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(state_machine_driver_->EndOngoingActivity(kActivityId, t_past), ZX_ERR_OUT_OF_RANGE);
}

TEST_F(StateMachineDriverTest, AllowsOldEventsIfAfterLastStateChange) {
  auto t1 = Now();
  auto t2 = t1 + zx::duration(zx::sec(1));
  auto t3 = t1 + zx::duration(zx::sec(2));
  // Advances time to t3, but the last transition time is still t1 since no events
  // were received
  RunLoopUntil(t3);

  // Events at t2 (which is < t3 but still after the last state change t1) should still be handled
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), t2), ZX_OK);
  RunLoopUntilIdle();
}

TEST_F(StateMachineDriverTest, BecomesActiveOnDiscreteActivity) {
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(StateMachineDriverTest, BecomesActiveOnActivityStart) {
  ASSERT_EQ(state_machine_driver_->StartOngoingActivity(kActivityId, Now()), ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(StateMachineDriverTest, BecomesActiveOnSpuriousActivityEnd) {
  ASSERT_EQ(state_machine_driver_->EndOngoingActivity(kActivityId, Now()), ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(StateMachineDriverTest, BecomesIdleOnTimeout) {
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(StateMachineDriverTest, RepeatedActivitiesResetTimer) {
  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  // Run until the timer is very close to expiring, but hasn't expired yet
  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  ASSERT_GE(*timeout, zx::duration(zx::msec(1)));
  RunLoopFor((*timeout) - zx::duration(zx::msec(1)));
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  // Run the timer close to completion again. The timer should have reset, so we should not
  // trigger the timer.
  RunLoopFor((*timeout) - zx::duration(zx::msec(1)));
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);
}


TEST_F(StateMachineDriverTest, IgnoresTimeoutsIfActivityStarted) {
  ASSERT_EQ(state_machine_driver_->StartOngoingActivity(kActivityId, Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  // Ending the activity allows the next timeout to proceed
  ASSERT_EQ(state_machine_driver_->EndOngoingActivity(kActivityId, Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  RunLoopFor(*timeout);
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(StateMachineDriverTest, HandlesTimeoutsIfActivitySpuriouslyEnded) {
  ASSERT_EQ(state_machine_driver_->EndOngoingActivity(kActivityId, Now()), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(StateMachineDriverTest, InvokesCallbackOnStateChanges) {
  int calls = 0;
  fuchsia::ui::activity::State observed_state = fuchsia::ui::activity::State::UNKNOWN;
  StateChangedCallback callback{
      [&calls, &observed_state](fuchsia::ui::activity::State state, __UNUSED zx::time time) {
        calls++;
        observed_state = state;
      }};
  state_machine_driver_->SetStateChangedCallback(std::move(callback));

  ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(observed_state, fuchsia::ui::activity::State::ACTIVE);

  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(observed_state, fuchsia::ui::activity::State::IDLE);
}

TEST_F(StateMachineDriverTest, TimeoutsIgnoredIfObjectDestroyedBeforeExpiry) {
  int calls = 0;
  StateChangedCallback callback{
      [&calls](__UNUSED fuchsia::ui::activity::State state, __UNUSED zx::time time) { calls++; }};
  {
    StateMachineDriver driver(dispatcher());
    ASSERT_EQ(state_machine_driver_->ReceiveDiscreteActivity(DiscreteActivity(), Now()), ZX_OK);
    RunLoopUntilIdle();
    ASSERT_EQ(state_machine_driver_->state(), fuchsia::ui::activity::State::ACTIVE);

    driver.SetStateChangedCallback(std::move(callback));
  }
  auto timeout = ActivityStateMachine::TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  // The driver was destroyed, so the callback should not have been invoked across a state change
  // because the reference in the async task to the driver ought to have been invalidated.
  EXPECT_EQ(calls, 0);
}

}  // namespace activity_service
