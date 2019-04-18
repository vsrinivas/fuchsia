// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/settings_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "gtest/gtest.h"

// DEPRECATED

namespace a11y_manager {
namespace {

using namespace fuchsia::accessibility;

struct ExpectedSettings {
  bool magnification_enabled = false;
  float magnification_zoom_factor = 1.0;
  bool screen_reader_enabled = false;
  bool color_inversion_enabled = false;
  ColorCorrection color_correction = ColorCorrection::DISABLED;
};

class SettingsManagerUnitTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    provider_.service_directory_provider()->AddService<SettingsManager>(
        [this](fidl::InterfaceRequest<SettingsManager> request) {
          settings_manager_.AddBinding(std::move(request));
        });

    RunLoopUntilIdle();
  }

  void VerifySettings(const ExpectedSettings& expected_settings) {
    auto status = SettingsManagerStatus::OK;
    Settings settings;
    settings_manager_.GetSettings(
        [&status, &settings](SettingsManagerStatus ret_status,
                             Settings ret_settings) {
          status = ret_status;
          fidl::Clone(ret_settings, &settings);
        });

    ASSERT_EQ(status, SettingsManagerStatus::OK);

    ASSERT_EQ(settings.magnification_enabled(),
              expected_settings.magnification_enabled);
    ASSERT_EQ(settings.magnification_zoom_factor(),
              expected_settings.magnification_zoom_factor);
    ASSERT_EQ(settings.screen_reader_enabled(),
              expected_settings.screen_reader_enabled);
    ASSERT_EQ(settings.color_inversion_enabled(),
              expected_settings.color_inversion_enabled);
    ASSERT_EQ(settings.color_correction(), expected_settings.color_correction);
  }

  sys::testing::ComponentContextProvider provider_;
  a11y_manager::SettingsManagerImpl settings_manager_;
};

TEST_F(SettingsManagerUnitTest, SetMagnificationEnabledFalseCurrentlyFalse) {
  auto status = SettingsManagerStatus::OK;

  // Attempt to set magnification_enabled = false when magnification is
  // already disabled.
  // Settings should remain unchanged, and return status should be OK.
  settings_manager_.SetMagnificationEnabled(
      false, [&status](SettingsManagerStatus retval) { status = retval; });

  RunLoopUntilIdle();

  ASSERT_EQ(status, SettingsManagerStatus::OK);
  // Expect default settings to remain unchanged.
  ExpectedSettings expected_settings;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetMagnificationEnabledTrueCurrentlyFalse) {
  auto status = SettingsManagerStatus::OK;

  // Attempt to set magnification_enabled = true when magnification is
  // currently disabled.
  // magnfication_enabled should now be true, and return status should be OK.
  settings_manager_.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });

  RunLoopUntilIdle();

  ASSERT_EQ(status, SettingsManagerStatus::OK);
  // Expect default settings to remain unchanged.
  ExpectedSettings expected_settings;
  expected_settings.magnification_enabled = true;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetMagnificationEnabledTrueCurrentlyTrue) {
  auto status = SettingsManagerStatus::OK;

  // Test setup.
  // Set magnfication_enabled = true and set magnification_zoom_factor = 2.0.
  settings_manager_.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  settings_manager_.SetMagnificationZoomFactor(
      2.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings_post_setup;
  expected_settings_post_setup.magnification_enabled = true;
  expected_settings_post_setup.magnification_zoom_factor = 2.0;
  VerifySettings(expected_settings_post_setup);

  // Test body.
  // Attempt to set magnification_enabled = true. Settings should remain
  // unchanged, and return status should be OK.
  settings_manager_.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings;
  expected_settings.magnification_enabled = true;
  expected_settings.magnification_zoom_factor = 2.0;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetMagnificationEnabledFalseCurrentlyTrue) {
  auto status = SettingsManagerStatus::OK;

  // Test setup.
  // Set magnfication_enabled = true and set magnification_zoom_factor = 2.0.
  settings_manager_.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  settings_manager_.SetMagnificationZoomFactor(
      2.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings_post_setup;
  expected_settings_post_setup.magnification_enabled = true;
  expected_settings_post_setup.magnification_zoom_factor = 2.0;
  VerifySettings(expected_settings_post_setup);

  // Test body.
  // Attempt to set magnification_enabled = false. Settings should return to
  // default values, and return status should be OK.
  settings_manager_.SetMagnificationEnabled(
      false, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetZoomFactorMagnificationDisabled) {
  auto status = SettingsManagerStatus::OK;

  settings_manager_.SetMagnificationZoomFactor(
      2.0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::ERROR);

  ExpectedSettings expected_settings;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetMagnificationZoomFactorInvalidValue) {
  auto status = SettingsManagerStatus::OK;

  // Test setup.
  // Set magnfication_enabled = true.
  settings_manager_.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  settings_manager_.SetMagnificationZoomFactor(
      0, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::ERROR);

  ExpectedSettings expected_settings;
  expected_settings.magnification_enabled = true;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetScreenReaderEnabled) {
  auto status = SettingsManagerStatus::OK;

  settings_manager_.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings;
  expected_settings.screen_reader_enabled = true;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetColorInversionEnabled) {
  auto status = SettingsManagerStatus::OK;

  settings_manager_.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings;
  expected_settings.color_inversion_enabled = true;
  VerifySettings(expected_settings);
}

TEST_F(SettingsManagerUnitTest, SetColorCorrection) {
  auto status = SettingsManagerStatus::OK;

  settings_manager_.SetColorCorrection(
      ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, SettingsManagerStatus::OK);

  ExpectedSettings expected_settings;
  expected_settings.color_correction = ColorCorrection::CORRECT_PROTANOMALY;
  VerifySettings(expected_settings);
}

}  // namespace
}  // namespace a11y_manager
