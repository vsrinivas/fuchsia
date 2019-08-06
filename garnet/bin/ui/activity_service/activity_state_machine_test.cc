#include "garnet/bin/ui/activity_service/activity_state_machine.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include "gtest/gtest.h"

namespace activity_service {

TEST(ActivityStateMachine, BaseStateIdle) {
  ActivityStateMachine state_machine;
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);
}

TEST(ActivityStateMachine, ActiveToInactive) {
  ActivityStateMachine state_machine;
  state_machine.ReceiveEvent(Event::USER_INPUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::ACTIVE);

  // Subsequent events remain ACTIVE
  state_machine.ReceiveEvent(Event::USER_INPUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::ACTIVE);

  state_machine.ReceiveEvent(Event::TIMEOUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);

  // Timeouts are ignored while IDLE
  state_machine.ReceiveEvent(Event::TIMEOUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);
}

}  // namespace activity_service
