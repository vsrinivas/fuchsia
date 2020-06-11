// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/configuration/color_transform_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <cmath>

#include <gtest/gtest.h>

namespace accessibility_test {
namespace {

// clang-format off
const std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};
const std::array<float, 3> kZero3x1Vector = {0, 0, 0};

const std::array<float, 9> kColorInversionMatrix = {
    0.402f,  -1.174f,  -0.228f,
   -0.598f,  -0.174f,  -0.228f,
   -0.599f,  -1.177f,   0.771f};
const std::array<float, 3> kColorInversionPostOffset = {.999f, .999f, .999f};

const std::array<float, 9> kCorrectProtanomaly = {
    0.622774, 0.264275,  0.216821,
    0.377226, 0.735725,  -0.216821,
    0.000000, -0.000000, 1.000000};

const std::array<float, 9> kProtanomalyAndInversionMatrix = {
    -0.192508, -0.757502,  0.113709,
    -0.438056, -0.286052, -0.319932,
    -0.817036, -1.024249,  0.896322};
const std::array<float, 3> kProtanomalyAndInversionPostOffset = {.999f, .999f, .999f};

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
    pre_offset_ = configuration.has_color_adjustment_pre_offset()
                      ? configuration.color_adjustment_pre_offset()
                      : kZero3x1Vector;
    post_offset_ = configuration.has_color_adjustment_post_offset()
                       ? configuration.color_adjustment_post_offset()
                       : kZero3x1Vector;

    color_inversion_enabled_ = configuration.has_color_inversion_enabled()
                                   ? configuration.color_inversion_enabled()
                                   : false;

    color_correction_mode_ = configuration.has_color_correction()
                                 ? configuration.color_correction()
                                 : fuchsia::accessibility::ColorCorrectionMode::DISABLED;
    callback();
  }

  bool hasTransform(std::array<float, 9> transform_to_compare) const {
    return FloatArraysAreEqual(transform_, transform_to_compare);
  }

  bool hasPostOffset(std::array<float, 3> offset_to_compare) const {
    return FloatArraysAreEqual(post_offset_, offset_to_compare);
  }

  fidl::BindingSet<fuchsia::accessibility::ColorTransformHandler> bindings_;
  std::array<float, 9> transform_;
  std::array<float, 3> pre_offset_;
  std::array<float, 3> post_offset_;

  bool color_inversion_enabled_;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;

 private:
  template <size_t N>
  static bool FloatArraysAreEqual(const std::array<float, N>& a, const std::array<float, N>& b) {
    const float float_comparison_epsilon = 0.00001;
    for (size_t i = 0; i < N; i++) {
      if ((std::fabs(a[i] - b[i]) > float_comparison_epsilon)) {
        return false;
      }
    }
    return true;
  }
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
  EXPECT_TRUE(color_transform_handler_.hasPostOffset(kColorInversionPostOffset));
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
  EXPECT_TRUE(color_transform_handler_.hasTransform(kProtanomalyAndInversionMatrix));
  EXPECT_TRUE(color_transform_handler_.hasPostOffset(kProtanomalyAndInversionPostOffset));
}

}  // namespace
}  // namespace accessibility_test
