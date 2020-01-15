// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_control_connection.h"

#include <fuchsia/ui/activity/control/cpp/fidl.h>

#include <memory>

#include "garnet/public/lib/gtest/test_loop_fixture.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace activity {

class ActivityControlConnectionTest : public ::gtest::TestLoopFixture {
 public:
  ActivityControlConnectionTest() : driver_(dispatcher()) {}

  void SetUp() override {
    conn_ = std::make_unique<ActivityControlConnection>(&driver_, dispatcher(),
                                                        client_.NewRequest(dispatcher()));
  }

 protected:
  StateMachineDriver driver_;
  std::unique_ptr<ActivityControlConnection> conn_;
  fuchsia::ui::activity::control::ControlPtr client_;
};

TEST_F(ActivityControlConnectionTest, SetState) {
  client_->SetState(fuchsia::ui::activity::State::ACTIVE);
  RunLoopUntilIdle();
  EXPECT_EQ(driver_.state(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  // The state machine should now always report ACTIVE, even after timing out.
  EXPECT_EQ(driver_.GetState(), fuchsia::ui::activity::State::ACTIVE);
}

}  // namespace activity
