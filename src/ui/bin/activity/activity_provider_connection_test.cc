// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_provider_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <memory>

#include "garnet/public/lib/gtest/test_loop_fixture.h"
#include "src/ui/bin/activity/fake_listener.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace {

fuchsia::ui::activity::DiscreteActivity DiscreteActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::DiscreteActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

}  // namespace

namespace activity {

class ActivityProviderConnectionTest : public ::gtest::TestLoopFixture {
 public:
  ActivityProviderConnectionTest() : driver_(dispatcher()) {}

  void SetUp() override {
    conn_ = std::make_unique<ActivityProviderConnection>(
        &driver_, dispatcher(), client_.NewRequest(dispatcher()),
        ::testing::UnitTest::GetInstance()->random_seed());
  }

 protected:
  StateMachineDriver driver_;
  std::unique_ptr<ActivityProviderConnection> conn_;
  fuchsia::ui::activity::ProviderPtr client_;
};

TEST_F(ActivityProviderConnectionTest, ReceiveCurrentStateOnConnection) {
  testing::FakeListener listener;
  client_->WatchState(listener.NewHandle(dispatcher()));

  RunLoopUntilIdle();

  EXPECT_EQ(listener.StateChanges().size(), 1u);
}
TEST_F(ActivityProviderConnectionTest, SingleStateChange) {
  testing::FakeListener listener;
  client_->WatchState(listener.NewHandle(dispatcher()));

  driver_.ReceiveDiscreteActivity(DiscreteActivity(), Now(), []() {});
  RunLoopUntilIdle();

  EXPECT_EQ(listener.StateChanges().size(), 2u);
}

TEST_F(ActivityProviderConnectionTest, MultipleStateChanges) {
  testing::FakeListener listener;
  client_->WatchState(listener.NewHandle(dispatcher()));

  driver_.ReceiveDiscreteActivity(DiscreteActivity(), Now(), []() {});
  auto timeout = driver_.state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  EXPECT_EQ(listener.StateChanges().size(), 3u);
}

TEST_F(ActivityProviderConnectionTest, CleansUpListenerOnStop) {
  testing::FakeListener listener;
  client_->WatchState(listener.NewHandle(dispatcher()));

  conn_->Stop();
  EXPECT_EQ(driver_.num_observers(), 0u);
}

TEST_F(ActivityProviderConnectionTest, CleansUpListenerOnDestructor) {
  {
    testing::FakeListener listener;
    client_->WatchState(listener.NewHandle(dispatcher()));
  }
  EXPECT_EQ(driver_.num_observers(), 0u);
}

TEST_F(ActivityProviderConnectionTest, CleansUpListenerOnConnClose) {
  conn_.reset();
  EXPECT_EQ(driver_.num_observers(), 0u);
}

}  // namespace activity
