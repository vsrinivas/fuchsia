// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/a11y/a11y_manager/settings/settings_manager_impl.h"
#include "garnet/bin/a11y/tests/mocks/mock_settings_service.h"
#include "garnet/bin/a11y/tests/mocks/mock_settings_watcher.h"
#include "gtest/gtest.h"

namespace accessibility_test {
using fuchsia::accessibility::SettingsManagerStatus;

class SettingsManagerTest : public gtest::TestLoopFixture {
 public:
  explicit SettingsManagerTest() = default;

  ~SettingsManagerTest() = default;

  void SetUp() override {
    TestLoopFixture::SetUp();
    context_provider_.service_directory_provider()
        ->AddService<fuchsia::accessibility::SettingsManager>(
            [this](
                fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager>
                    request) {
              settings_manager_impl_.AddBinding(std::move(request));
            });

    RunLoopUntilIdle();
  }

  sys::testing::ComponentContextProvider* GetContextProvider() {
    return &context_provider_;
  }

  static const fuchsia::accessibility::Settings default_settings_;

 private:
  static const fuchsia::accessibility::Settings InitSettings() {
    fuchsia::accessibility::Settings settings;
    settings.set_magnification_enabled(false);
    settings.set_magnification_zoom_factor(1);
    settings.set_screen_reader_enabled(false);
    settings.set_color_inversion_enabled(false);
    settings.set_color_correction(
        fuchsia::accessibility::ColorCorrection::DISABLED);
    std::array<float, 9> matrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    settings.set_color_adjustment_matrix(matrix);
    return settings;
  }

  sys::testing::ComponentContextProvider context_provider_;
  a11y_manager::SettingsManagerImpl settings_manager_impl_;
};

const fuchsia::accessibility::Settings SettingsManagerTest::default_settings_ =
    InitSettings();

TEST_F(SettingsManagerTest, SetMagnificationEnabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Check Settings in Watcher is same as the default settings, since nothing
  // has changed.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Enable Magnification from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  {
    // Check Settings in Watcher to make sure it is updated.
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetMagnificationZoomFactor) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Magnification from Settings Service first in order to set Zoom
  // Factor.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Modify Magnification Zoom Factor from Settings Service.
  settings_service.SetMagnificationZoomFactor(
      5.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_zoom_factor(5.0);
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

// Zoom Factor should not change when Magnification is enabled twice.
TEST_F(SettingsManagerTest, SetMagnificationEnabled_AlreadyEnabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Magnification and Zoom Factor from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);
  settings_service.SetMagnificationZoomFactor(
      5.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    expected_settings->set_magnification_zoom_factor(5.0);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Enable Magnification again from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    expected_settings->set_magnification_zoom_factor(5.0);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetMagnificationDisabled_AlreadyDisabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Disable Magnification from Settings Service, when it is already disabled.
  settings_service.SetMagnificationEnabled(
      false, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetMagnificationDisabled_PreviouslyEnabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Magnification and set Zoom Factor from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);
  settings_service.SetMagnificationZoomFactor(
      5.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Disable Magnification and make sure Zoom factor is reset.
  settings_service.SetMagnificationEnabled(
      false, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

// Setting Service cannot set ZoomFactor without Enabling Magnification.
TEST_F(SettingsManagerTest,
       SetMagnificationZoomFactor_MagnificationIsDisabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Modify Magnification Zoom Factor from Settings Service.
  settings_service.SetMagnificationZoomFactor(
      5.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::ERROR);

  // Check Settings in Watcher to make sure nothing has changed.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

// Any Value less than 1 is an invalid value for Zoom Factor.
TEST_F(SettingsManagerTest, SetMagnificationZoomFactor_InvalidValue) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Magnification from Settings Service first in order to set Zoom
  // Factor.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Modify Magnification Zoom Factor from Settings Service with 0.
  settings_service.SetMagnificationZoomFactor(
      0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::ERROR);

  // Check Settings in Watcher to make sure it is updated and Zoom Factor has
  // not changed.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetScreenReaderEnabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Screen Reader from Settings Service.
  settings_service.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_screen_reader_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetScreenReaderDisabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Screen Reader from Settings Service.
  settings_service.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_screen_reader_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Disable Screen Reader.
  settings_service.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_screen_reader_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetColorInversionEnabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Color Inversion from Settings Service.
  settings_service.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_inversion_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetColorInversionDisabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Color Inversion from Settings Service.
  settings_service.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_inversion_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Disable Color Correction from Settings Service.
  settings_service.SetColorInversionEnabled(
      false, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_inversion_enabled(false);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetColorCorrection) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Color Correction from Settings Service.
  settings_service.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_correction(
        fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, SetColorCorrection_Disabled) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Color Correction from Settings Service.
  settings_service.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_correction(
        fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Change Color Correction back to Disabled.
  settings_service.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::DISABLED,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_color_correction(
        fuchsia::accessibility::ColorCorrection::DISABLED);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

// Check if watchers can register before a SettingService.
TEST_F(SettingsManagerTest, WatcherRegisteredBeforeSettingService) {
  auto status = SettingsManagerStatus::OK;

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());
  RunLoopUntilIdle();

  // Enable Magnification from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, WatcherRegisteredAfterSettingsAreModified) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Enable Magnification from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Check Settings in Watcher to make sure it is updated.
  auto expected_settings = fuchsia::accessibility::Settings::New();
  default_settings_.Clone(expected_settings.get());
  expected_settings->set_magnification_enabled(true);
  ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
}

TEST_F(SettingsManagerTest, MultipleWatchers) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service(GetContextProvider());

  // Add first watcher.
  MockSettingsWatcher first_watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Add second watcher.
  MockSettingsWatcher second_watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Check Settings in Watcher is same as the default settings, since nothing
  // has changed.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(first_watcher.IsSame(std::move(expected_settings)));
  }

  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(second_watcher.IsSame(std::move(expected_settings)));
  }

  // Enable Magnification from Settings Service.
  settings_service.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(first_watcher.IsSame(std::move(expected_settings)));
  }

  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(second_watcher.IsSame(std::move(expected_settings)));
  }
}

TEST_F(SettingsManagerTest, MultipleSettingsService_UseLastOne) {
  auto status = SettingsManagerStatus::OK;

  // Create Settings Service.
  MockSettingsService settings_service_first(GetContextProvider());
  RunLoopUntilIdle();
  // Create second Settings service.
  MockSettingsService settings_service_second(GetContextProvider());

  // Add a watcher.
  MockSettingsWatcher watcher(GetContextProvider());
  RunLoopUntilIdle();

  // Check Settings in Watcher is same as the default settings, since nothing
  // has changed.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Enable Magnification from First Settings Service. This should have no
  // effect on settings, since this no longer registered to Settings Manager.
  FXL_LOG(INFO) << "Following FIDL error message is expected since a FIDL call "
                   "is made on a channel which no longer exist.";
  settings_service_first.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();

  // Check Settings in Watcher. Settings should not change..
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }

  // Enable Magnification from second Settings Service. This should change
  // Settings.
  settings_service_second.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  // Check Settings in Watcher to make sure it is updated.
  {
    auto expected_settings = fuchsia::accessibility::Settings::New();
    default_settings_.Clone(expected_settings.get());
    expected_settings->set_magnification_enabled(true);
    ASSERT_TRUE(watcher.IsSame(std::move(expected_settings)));
  }
}

}  // namespace accessibility_test
