// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/activity_listener_impl.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <cstdio>

#include <gtest/gtest.h>

#include "sdk/lib/sys/cpp/testing/service_directory_provider.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace cobalt {

using fuchsia::ui::activity::State;
namespace activity = fuchsia::ui::activity;

class FakeActivity : public activity::Provider {
 public:
  fidl::InterfaceRequestHandler<activity::Provider> GetHandler() {
    return [this](fidl::InterfaceRequest<activity::Provider> request) {
      binding_ = std::make_unique<fidl::Binding<activity::Provider>>(this, std::move(request));
    };
  }

  void WatchState(::fidl::InterfaceHandle<class activity::Listener> listener) override {
    listener_ = listener.Bind();
    SendUpdate();
  }

  void SetState(activity::State state) {
    state_ = state;
    SendUpdate();
  }

  void CloseConnection() {
    if (binding_) {
      binding_->Unbind();
    }
  }

 private:
  void SendUpdate() {
    listener_->OnStateChanged(state_, 0, []() {});
  }

  std::unique_ptr<fidl::Binding<fuchsia::ui::activity::Provider>> binding_;
  ::fidl::InterfacePtr<fuchsia::ui::activity::Listener> listener_;
  activity::State state_ = activity::State::UNKNOWN;
};
class ActivityListenerTest : public gtest::TestLoopFixture,
                             public testing::WithParamInterface<std::optional<bool>> {
 public:
  ActivityListenerTest() : service_directory_provider_(dispatcher()) {}

  void SetUp() override {
    activity_service_ = std::make_unique<FakeActivity>();
    service_directory_provider_.AddService(activity_service_->GetHandler());

    listener_ = std::make_unique<ActivityListenerImpl>(
        dispatcher(), service_directory_provider_.service_directory());
    listener_->Start([this](ActivityState state) { Callback(state); });
    RunLoopUntilIdle();
  }

  void Reconnect() { service_directory_provider_.AddService(activity_service_->GetHandler()); }

  void Callback(ActivityState state) { current_state_ = state; }

 protected:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<FakeActivity> activity_service_;
  std::unique_ptr<ActivityListenerImpl> listener_;
  ActivityState current_state_ = ActivityState::UNKNOWN;
};

TEST_F(ActivityListenerTest, DefaultsToStateUnknown) {
  EXPECT_FALSE(listener_->IsConnected());
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);
}

TEST_F(ActivityListenerTest, InvokesCallbackWithCorrectState) {
  activity_service_->SetState(fuchsia::ui::activity::State::ACTIVE);
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);

  activity_service_->SetState(fuchsia::ui::activity::State::IDLE);
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);

  activity_service_->SetState(fuchsia::ui::activity::State::UNKNOWN);
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);
}

TEST_F(ActivityListenerTest, StateResetIfServerNotAvailable) {
  current_state_ = ActivityState::IDLE;

  activity_service_->CloseConnection();
  RunLoopUntilIdle();

  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);
}

TEST_F(ActivityListenerTest, OverridesCallback) {
  activity_service_->SetState(fuchsia::ui::activity::State::ACTIVE);
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);

  listener_->Start([](ActivityState state) {});
  activity_service_->SetState(fuchsia::ui::activity::State::UNKNOWN);
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);

  activity_service_->SetState(fuchsia::ui::activity::State::IDLE);
  listener_->Start([this](ActivityState state) { Callback(state); });
  RunLoopUntilIdle();
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);
}

TEST_F(ActivityListenerTest, ReconnectsIfServerClosesConnection) {
  activity_service_->CloseConnection();
  activity_service_->SetState(fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);

  Reconnect();
  RunLoopFor(zx::msec(200));
  EXPECT_EQ(current_state_, ActivityState::IDLE);
  EXPECT_EQ(listener_->state(), ActivityState::IDLE);
}

}  // namespace cobalt
