// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "gtest/gtest.h"

namespace {
class Dependency : public scenic_impl::System {
 public:
  using System::System;
  scenic_impl::CommandDispatcherUniquePtr CreateCommandDispatcher(
      scenic_impl::CommandDispatcherContext context) override {
    ++num_dispatchers_;
    // We don't actually expect anyone to call this, but for logging purposes, we will record it.
    return nullptr;
  };

  uint32_t GetNumDispatchers() { return num_dispatchers_; }

 private:
  uint32_t num_dispatchers_ = 0;
};

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

TEST_F(ScenicTest, SessionCreatedAfterAllSystemsInitialized) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);

  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Request session creation, which doesn't occur yet because system isn't
  // initialized.
  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // Initializing the system allows the session to be created.
  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
}

TEST_F(ScenicTest, DependenciesBlockSessionCreation) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto dependency = std::make_unique<Dependency>(
      SystemContext(scenic_->app_context(), inspect_deprecated::Node(), /*quit_callback*/ nullptr),
      false);
  scenic()->RegisterDependency(dependency.get());

  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // This should not create the session, as the dependency is still uninitialized.
  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 0U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);

  // At this point, all systems are initialized, but we don't dispatch to dependencies.
  dependency->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);
}

TEST_F(ScenicTest, DependenciesBlockSessionCreationReverseOrder) {
  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  auto dependency = std::make_unique<Dependency>(
      SystemContext(scenic_->app_context(), inspect_deprecated::Node(), /*quit_callback*/ nullptr),
      false);
  scenic()->RegisterDependency(dependency.get());

  auto session = CreateSession();
  EXPECT_EQ(scenic()->num_sessions(), 0U);

  // This test is identical to DependenciesBlockSessionCreation, but it initializes the dependency
  // first.
  dependency->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 0U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 0U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);

  mock_system->SetToInitialized();
  EXPECT_EQ(scenic()->num_sessions(), 1U);
  EXPECT_EQ(mock_system->GetNumDispatchers(), 1U);
  EXPECT_EQ(dependency->GetNumDispatchers(), 0U);
}

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

TEST_F(ScenicGfxTest, ScenicApiRaceBeforeSystemRegistration) {
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

  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  scenic()->SetDelegate(&delegate);

  EXPECT_FALSE(display_info);
  EXPECT_FALSE(screenshot);
  EXPECT_FALSE(display_ownership);

  mock_system->SetToInitialized();

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

TEST_F(ScenicGfxTest, ScenicApiRaceAfterSystemRegistration) {
  Delegate delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);

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

  mock_system->SetToInitialized();

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);
}

TEST_F(ScenicGfxTest, ScenicApiAfterDelegate) {
  Delegate delegate;

  bool display_info = false;
  auto display_info_callback = [&](fuchsia::ui::gfx::DisplayInfo info) { display_info = true; };

  bool screenshot = false;
  auto screenshot_callback = [&](fuchsia::ui::scenic::ScreenshotData data, bool status) {
    screenshot = true;
  };

  bool display_ownership = false;
  auto display_ownership_callback = [&](zx::event event) { display_ownership = true; };

  auto mock_system = scenic()->RegisterSystem<DummySystem>(false);
  scenic()->SetDelegate(&delegate);

  scenic()->GetDisplayInfo(display_info_callback);
  scenic()->TakeScreenshot(screenshot_callback);
  scenic()->GetDisplayOwnershipEvent(display_ownership_callback);

  EXPECT_TRUE(display_info);
  EXPECT_TRUE(screenshot);
  EXPECT_TRUE(display_ownership);

  mock_system->SetToInitialized();
}

}  // namespace test
}  // namespace scenic_impl
