// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/session.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scenic/take_screenshot_delegate_deprecated.h"
#include "src/ui/scenic/lib/scenic/tests/dummy_system.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace {

class DisplayInfoDelegate : public scenic_impl::Scenic::GetDisplayInfoDelegateDeprecated {
 public:
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override {
    auto info = fuchsia::ui::gfx::DisplayInfo();
    callback(std::move(info));
  }

  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override {
    zx::event event;
    callback(std::move(event));
  }
};

class TakeScreenshotDelegate : public scenic_impl::TakeScreenshotDelegateDeprecated {
 public:
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override {
    fuchsia::ui::scenic::ScreenshotData data;
    callback(std::move(data), true);
  }
};

}  // namespace

namespace scenic_impl {
namespace test {

class ScenicUnitTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    scenic_ = std::make_unique<Scenic>(
        provider_.context(), inspect_node_, frame_scheduler_, [this] { QuitLoop(); },
        use_flatland_);
  }

  std::unique_ptr<scenic::Session> CreateSession() {
    fuchsia::ui::scenic::SessionPtr session_ptr;
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
    fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
        listener_handle.NewRequest();
    scenic_->CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
    return std::make_unique<::scenic::Session>(std::move(session_ptr), std::move(listener_request));
  }

  bool use_flatland_ = false;
  sys::testing::ComponentContextProvider provider_;
  scheduling::test::MockFrameScheduler frame_scheduler_;
  inspect::Node inspect_node_;
  std::unique_ptr<Scenic> scenic_;
};

TEST_F(ScenicUnitTest, CreateAndDestroySession) {
  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  EXPECT_EQ(scenic_->num_sessions(), 0U);

  auto session = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_NE(mock_system->GetLastSessionId(), -1);

  // Closing the session should cause another update to be scheduled.
  bool update_scheduled = false;
  frame_scheduler_.set_schedule_update_for_session_callback(
      [&update_scheduled](auto...) { update_scheduled = true; });
  scenic_->CloseSession(mock_system->GetLastSessionId());
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  EXPECT_TRUE(update_scheduled);
}

TEST_F(ScenicUnitTest, CreateAndDestroySession_TableVariant) {
  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  EXPECT_EQ(scenic_->num_sessions(), 0U);

  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  scenic_->CreateSessionT(std::move(endpoints), [] {});
  auto session =
      std::make_unique<::scenic::Session>(std::move(session_ptr), std::move(listener_request));

  EXPECT_EQ(scenic_->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_NE(mock_system->GetLastSessionId(), -1);

  // Closing the session should cause another update to be scheduled.
  bool update_scheduled = false;
  frame_scheduler_.set_schedule_update_for_session_callback(
      [&update_scheduled](auto...) { update_scheduled = true; });
  scenic_->CloseSession(mock_system->GetLastSessionId());
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  EXPECT_TRUE(update_scheduled);
}

TEST_F(ScenicUnitTest, CreateAndDestroyMultipleSessions) {
  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  EXPECT_EQ(scenic_->num_sessions(), 0U);

  auto session1 = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  auto session1_id = mock_system->GetLastSessionId();
  EXPECT_NE(session1_id, -1);

  auto session2 = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 2U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 2U);
  auto session2_id = mock_system->GetLastSessionId();
  EXPECT_NE(session2_id, -1);

  auto session3 = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 3U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 3U);
  auto session3_id = mock_system->GetLastSessionId();
  EXPECT_TRUE(session3_id);

  scenic_->CloseSession(session2_id);
  EXPECT_EQ(scenic_->num_sessions(), 2U);

  scenic_->CloseSession(session3_id);
  EXPECT_EQ(scenic_->num_sessions(), 1U);

  scenic_->CloseSession(session1_id);
  EXPECT_EQ(scenic_->num_sessions(), 0U);
}

TEST_F(ScenicUnitTest, InvalidEndpointTable) {
  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  EXPECT_EQ(scenic_->num_sessions(), 0U);

  fuchsia::ui::scenic::SessionEndpoints empty;  // Requires .session field, don't put one in.
  bool ack = false;
  scenic_->CreateSessionT(std::move(empty), [&ack] { ack = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(ack);
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 0U);
  EXPECT_EQ(mock_system->GetLastSessionId(), -1);
}

TEST_F(ScenicUnitTest, InvalidPresentCall_ShouldDestroySession) {
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  auto session = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 1U);

  session->Present(/*Presentation Time*/ 10,
                   /*Present Callback*/ [](auto) {});

  // Trigger error by making a present call with an earlier presentation time
  // than the previous call to present
  session->Present(/*Presentation Time*/ 0,
                   /*Present Callback*/ [](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(scenic_->num_sessions(), 0U);
}

TEST_F(ScenicUnitTest, InvalidPresent2Call_ShouldDestroySession) {
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  auto session = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 1U);

  session->Present2(/*requested_presentation_time=*/10, /*requested_prediction_span=*/0,
                    /*immediate_callback=*/[](auto) {});

  // Trigger error by making a Present2 call with an earlier presentation time
  // than the previous call to Present2.
  session->Present2(/*requested_presentation_time=*/0, /*requested_prediction_span=*/0,
                    /*immediate_callback=*/[](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(scenic_->num_sessions(), 0U);
}

TEST_F(ScenicUnitTest, FailedUpdate_ShouldDestroySession) {
  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  EXPECT_EQ(scenic_->num_sessions(), 0U);
  auto session = CreateSession();
  EXPECT_EQ(scenic_->num_sessions(), 1U);

  // Mark the session as having failed an update next time DummySystem runs UpdateSessions().
  auto id = mock_system->GetLastSessionId();
  ASSERT_GE(id, 0);
  scheduling::SessionId session_id = static_cast<scheduling::SessionId>(id);
  mock_system->SetUpdateSessionsReturnValue({.sessions_with_failed_updates = {session_id}});

  // Check that the next update causes Session destruction.
  EXPECT_EQ(scenic_->num_sessions(), 1U);
  auto update_result = scenic_->UpdateSessions(/*sessions_to_update*/ {}, /*frame_trace_id*/ 23);
  EXPECT_EQ(scenic_->num_sessions(), 0U);

  // Returned |update_result| should contain the same sessions returned from the system.
  ASSERT_EQ(update_result.sessions_with_failed_updates.size(), 1u);
  EXPECT_EQ(*update_result.sessions_with_failed_updates.begin(), session_id);
}

TEST_F(ScenicUnitTest, ScenicApiAfterDelegate) {
  DisplayInfoDelegate display_info_delegate;
  TakeScreenshotDelegate screenshot_delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  auto mock_system = scenic_->RegisterSystem<DummySystem>();
  scenic_->SetDisplayInfoDelegate(&display_info_delegate);
  scenic_->SetScreenshotDelegate(&screenshot_delegate);

  scenic_->GetDisplayInfo(display_info_callback);
  scenic_->TakeScreenshot(screenshot_callback);
  scenic_->GetDisplayOwnershipEvent(display_ownership_callback);

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

// Tests that Scenic returns the flatland signal it is initialized with.
class ParameterizedScenicUsesFlatlandTest : public ScenicUnitTest,
                                            public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    use_flatland_ = GetParam();
    ScenicUnitTest::SetUp();
  }
};

TEST_P(ParameterizedScenicUsesFlatlandTest, UsesFlatlandCallbackIsRun) {
  std::optional<bool> uses_flatland;
  scenic_->UsesFlatland([&uses_flatland](bool enabled) { uses_flatland = enabled; });
  EXPECT_TRUE(uses_flatland.has_value());
  EXPECT_EQ(*uses_flatland, GetParam());
}

INSTANTIATE_TEST_SUITE_P(UseFlatland, ParameterizedScenicUsesFlatlandTest, ::testing::Bool());

}  // namespace test
}  // namespace scenic_impl
