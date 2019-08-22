// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/a11y_settings_watcher.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/testing/test_with_context.h>
#include <lib/ui/scenic/cpp/session.h>

#include <gtest/gtest.h>

#include "garnet/bin/ui/root_presenter/tests/fakes/fake_scenic.h"
#include "garnet/bin/ui/root_presenter/tests/fakes/fake_settings_service.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/a11y/lib/settings/settings_manager.h"
#include "src/ui/a11y/lib/settings/settings_provider.h"

namespace root_presenter {
namespace testing {
namespace {
using fuchsia::accessibility::Settings;
using fuchsia::accessibility::SettingsManagerStatus;
using fuchsia::accessibility::SettingsPtr;

// clang-format off
const std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};
// clang-format on

class A11ySettingsWatcherTest : public component::testing::TestWithContext {
 public:
  void SetUp() override {
    // Register the fake Scenic service with the environment.  This must
    // happen before calling |TakeContext|.
    //    controller().AddService(fake_scenic_.GetHandler());
    RunLoopUntilIdle();
    controller().AddService<fuchsia::accessibility::SettingsManager>(
        [this](fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
          settings_manager_impl_.AddBinding(std::move(request));
        });
    RunLoopUntilIdle();
    startup_context_ = TakeContext();
  }

  static const fuchsia::accessibility::Settings InitSettings() {
    fuchsia::accessibility::Settings settings;
    settings.set_magnification_enabled(false);
    settings.set_magnification_zoom_factor(1);
    settings.set_screen_reader_enabled(false);
    settings.set_color_inversion_enabled(false);
    settings.set_color_correction(fuchsia::accessibility::ColorCorrection::DISABLED);
    std::array<float, 9> matrix = kIdentityMatrix;
    settings.set_color_adjustment_matrix(matrix);
    return settings;
  }

  FakeScenic fake_scenic_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<component::StartupContext> startup_context_;
  a11y::SettingsManager settings_manager_impl_;
};

// Basic test to make sure Settings watcher can connect to A11y Settings and get
// updates.
TEST_F(A11ySettingsWatcherTest, VerifyA11ySettingsWatcher) {
  // Create Scenic and Session.
  scenic::ResourceId id = 1;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();
  fake_scenic_.CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  session_ = std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));

  // Create Settings Watcher and connect to A11y Settings Service.
  std::unique_ptr<A11ySettingsWatcher> settings_watcher =
      std::make_unique<A11ySettingsWatcher>(startup_context_.get(), id, session_.get());
  RunLoopUntilIdle();

  // Create Settings Service which will change settings.
  FakeSettingsService settings_provider(startup_context_.get());
  RunLoopUntilIdle();

  // Check if Settings Watcher recieves default settings on connection.
  float kDefaultZoomFactor = 1.0;
  SettingsPtr settings = settings_watcher->CloneA11ySettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_FALSE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kDefaultZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_FALSE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_FALSE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::DISABLED, settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
  EXPECT_EQ(kIdentityMatrix, settings->color_adjustment_matrix());

  // Change settings and verify the changes are reflected in App.
  SettingsManagerStatus status = SettingsManagerStatus::OK;
  settings_provider.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetMagnificationZoomFactor(
      10, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);

  // Verify new settings in App.
  float kExpectedZoomFactor = 10.0;
  settings = settings_watcher->CloneA11ySettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_TRUE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kExpectedZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_TRUE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_TRUE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
            settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
}

// Test to make sure Root Presenter doesn't crash when Empty Settings are passed to
// Settings Watcher.
TEST_F(A11ySettingsWatcherTest, SetSettingsWithEmptyInput) {
  // Create Scenic and Session.
  scenic::ResourceId id = 1;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
  fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
      listener_handle.NewRequest();
  fake_scenic_.CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
  session_ = std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));

  // Create Settings Watcher and connect to A11y Settings Service.
  std::unique_ptr<A11ySettingsWatcher> settings_watcher =
      std::make_unique<A11ySettingsWatcher>(startup_context_.get(), id, session_.get());
  RunLoopUntilIdle();

  Settings provided_settings;
  settings_watcher->OnSettingsChange(std::move(provided_settings));

  SettingsPtr settings = settings_watcher->CloneA11ySettings();
  ;
  float kExpectedZoomFactor = 1.0;
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_FALSE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_FALSE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kExpectedZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_FALSE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::DISABLED, settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
}

}  // namespace
}  // namespace testing
}  // namespace root_presenter
