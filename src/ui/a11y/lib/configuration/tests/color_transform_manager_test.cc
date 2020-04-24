// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/configuration/color_transform_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <cmath>

#include <src/lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace accessibility_test {

// clang-format off
const std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};

const std::array<float, 9> kColorInversionMatrix = {
    0.402, -0.598, -0.599,
    -1.174, -0.174, -1.175,
    -0.228, -0.228, 0.772};

const std::array<float, 9> kCorrectProtanomaly = {
    0.622774, 0.264275,  0.216821,
    0.377226, 0.735725,  -0.216821,
    0.000000, -0.000000, 1.000000};

const std::array<float, 9> kProtanomalyAndInversion = {
    0.024774,  -0.333725, -0.382179,
    -0.796774, -0.438275, -1.39182,
    -0.228,    -0.228,    0.772};

// clang-format on

class FakeColorTransformHandler : public fuchsia::accessibility::ColorTransformHandler {
 public:
  FakeColorTransformHandler() = default;
  ~FakeColorTransformHandler() = default;

  fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> GetHandle() {
    return bindings_.AddBinding(this);
  }

  // |fuchsia.accessibility.ColorTransformHandler|
  void SetColorTransformConfiguration(
      fuchsia::accessibility::ColorTransformConfiguration configuration,
      SetColorTransformConfigurationCallback callback) {
    transform_ = configuration.has_color_adjustment_matrix()
                     ? configuration.color_adjustment_matrix()
                     : kIdentityMatrix;

    color_inversion_enabled_ = configuration.has_color_inversion_enabled()
                                   ? configuration.color_inversion_enabled()
                                   : false;

    color_correction_mode_ = configuration.has_color_correction()
                                 ? configuration.color_correction()
                                 : fuchsia::accessibility::ColorCorrectionMode::DISABLED;
    callback();
  }

  bool hasTransform(std::array<float, 9> transform_to_compare) const {
    const float float_comparison_epsilon = 0.00001;
    for (int i = 0; i < 9; i++) {
      if ((std::fabs(transform_to_compare[i] - transform_[i]) > float_comparison_epsilon)) {
        return false;
      }
    }
    return true;
  }

  fidl::BindingSet<fuchsia::accessibility::ColorTransformHandler> bindings_;
  std::array<float, 9> transform_;
  bool color_inversion_enabled_;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;
};

class ColorTransformManagerTest : public gtest::TestLoopFixture {
 public:
  ColorTransformManagerTest() = default;
  ~ColorTransformManagerTest() override = default;

  void SetUp() override {
    TestLoopFixture::SetUp();

    startup_context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    color_transform_manager_ =
        std::make_unique<a11y::ColorTransformManager>(startup_context_.get());

    RunLoopUntilIdle();
  }

  std::unique_ptr<sys::ComponentContext> startup_context_;
  std::unique_ptr<a11y::ColorTransformManager> color_transform_manager_;
  FakeColorTransformHandler color_transform_handler_;
  fidl::BindingSet<fuchsia::accessibility::ColorTransform> color_transform_bindings_;
};

TEST_F(ColorTransformManagerTest, NoHandler) {
  // change a setting
  color_transform_manager_->ChangeColorTransform(
      false, fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  RunLoopUntilIdle();

  // This test is verifying that nothing crashes.
}

TEST_F(ColorTransformManagerTest, SetColorTransformDefault) {
  // register a (fake) handler
  color_transform_manager_->RegisterColorTransformHandler(color_transform_handler_.GetHandle());

  // change a setting
  color_transform_manager_->ChangeColorTransform(
      false, fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  RunLoopUntilIdle();

  // Verify handler gets sent the correct settings.
  EXPECT_FALSE(color_transform_handler_.color_inversion_enabled_);
  EXPECT_EQ(color_transform_handler_.color_correction_mode_,
            fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  EXPECT_TRUE(color_transform_handler_.hasTransform(kIdentityMatrix));
}

TEST_F(ColorTransformManagerTest, SetColorInversionEnabled) {
  // register a (fake) handler
  color_transform_manager_->RegisterColorTransformHandler(color_transform_handler_.GetHandle());

  // change a setting
  color_transform_manager_->ChangeColorTransform(
      true, fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  RunLoopUntilIdle();

  // Verify handler gets sent the correct settings.
  EXPECT_TRUE(color_transform_handler_.color_inversion_enabled_);
  EXPECT_EQ(color_transform_handler_.color_correction_mode_,
            fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  EXPECT_TRUE(color_transform_handler_.hasTransform(kColorInversionMatrix));
}

TEST_F(ColorTransformManagerTest, SetColorCorrection) {
  // register a (fake) handler
  color_transform_manager_->RegisterColorTransformHandler(color_transform_handler_.GetHandle());

  // change a setting
  color_transform_manager_->ChangeColorTransform(
      false, fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY);
  RunLoopUntilIdle();

  // Verify handler gets sent the correct settings.
  EXPECT_FALSE(color_transform_handler_.color_inversion_enabled_);
  EXPECT_EQ(color_transform_handler_.color_correction_mode_,
            fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY);
  EXPECT_TRUE(color_transform_handler_.hasTransform(kCorrectProtanomaly));
}

TEST_F(ColorTransformManagerTest, SetColorCorrectionAndInversion) {
  // register a (fake) handler
  color_transform_manager_->RegisterColorTransformHandler(color_transform_handler_.GetHandle());

  // change a setting
  color_transform_manager_->ChangeColorTransform(
      true, fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY);
  RunLoopUntilIdle();

  // Verify handler gets sent the correct settings.
  EXPECT_TRUE(color_transform_handler_.color_inversion_enabled_);
  EXPECT_EQ(color_transform_handler_.color_correction_mode_,
            fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY);
  EXPECT_TRUE(color_transform_handler_.hasTransform(kProtanomalyAndInversion));
}

};  // namespace accessibility_test
