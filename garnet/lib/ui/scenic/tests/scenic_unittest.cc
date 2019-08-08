// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "gtest/gtest.h"

namespace {
class Delegate : public scenic_impl::TempScenicDelegate {
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override {
    auto info = ::fuchsia::ui::gfx::DisplayInfo();
    callback(std::move(info));
  };
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override {
    fuchsia::ui::scenic::ScreenshotData data;
    callback(std::move(data), true);
  };
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override {
    zx::event event;
    callback(std::move(event));
  };
};

}  // namespace

namespace scenic_impl {
namespace test {

TEST_F(ScenicTest, CreateAndDestroySession) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>();
  scenic()->SetInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_TRUE(mock_system->GetLastSession());

  scenic()->CloseSession(mock_system->GetLastSession());
  EXPECT_EQ(scenic()->num_sessions(), 0U);
}

TEST_F(ScenicTest, SessionCreatedAfterInitialization) {
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Request session creation, which doesn't occur yet because system isn't
  // initialized.
  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Initializing Scenic allows the session to be created.
  scenic()->SetInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
}

// TODO(SCN-421)): This test requires a GfxSystem because GfxSystem is currently the source of
// TempSessionDelegates. Once this bug has been fixed, this test should revert back to a ScenicTest.
TEST_F(ScenicGfxTest, InvalidPresentCall_ShouldDestroySession) {
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 1U);

  session->Present(/*Presentation Time*/ 10,
                   /*Present Callback*/ [](auto) {});

  // Trigger error by making a present call with an earlier presentation time
  // than the previous call to present
  session->Present(/*Presentation Time*/ 0,
                   /*Present Callback*/ [](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(scenic()->num_sessions(), 0U);
}

TEST_F(ScenicTest, ScenicApiRaceBeforeSystemRegistration) {
  Delegate delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  scenic()->GetDisplayInfo(display_info_callback);
  scenic()->TakeScreenshot(screenshot_callback);
  scenic()->GetDisplayOwnershipEvent(display_ownership_callback);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  auto mock_system = scenic()->RegisterSystem<DummySystem>();
  scenic()->SetDelegate(&delegate);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  scenic()->SetInitialized();

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

TEST_F(ScenicTest, ScenicApiRaceAfterSystemRegistration) {
  Delegate delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  auto mock_system = scenic()->RegisterSystem<DummySystem>();

  scenic()->GetDisplayInfo(display_info_callback);
  scenic()->TakeScreenshot(screenshot_callback);
  scenic()->GetDisplayOwnershipEvent(display_ownership_callback);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  scenic()->SetDelegate(&delegate);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  scenic()->SetInitialized();

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

TEST_F(ScenicTest, ScenicApiAfterDelegate) {
  Delegate delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  auto mock_system = scenic()->RegisterSystem<DummySystem>();
  scenic()->SetDelegate(&delegate);

  scenic()->GetDisplayInfo(display_info_callback);
  scenic()->TakeScreenshot(screenshot_callback);
  scenic()->GetDisplayOwnershipEvent(display_ownership_callback);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  scenic()->SetInitialized();

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

}  // namespace test
}  // namespace scenic_impl
