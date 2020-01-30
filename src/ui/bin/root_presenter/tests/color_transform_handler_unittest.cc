// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/color_transform_handler.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/session.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_color_transform_manager.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_scenic.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_session.h"

namespace root_presenter {
namespace testing {
// clang-format off
const std::array<float, 9> kCorrectDeuteranomaly = {
    0.288299f, 0.052709f, -0.257912f,
    0.711701f, 0.947291f, 0.257912f,
    0.000000f, -0.000000f, 1.000000f};

const std::array<float, 9> kTint = {
    0.2f, 0.0f, -0.0f,
    0.2f, 0.0f, -0.0f,
    0.000000f, -0.000000f, 1.000000f};
// clang-format on

const scenic::ResourceId id = 1;

class ColorTransformHandlerTest : public gtest::TestLoopFixture {
 public:
  ColorTransformHandlerTest() {
    context_provider_.service_directory_provider()->AddService(fake_scenic_.GetHandler());
    context_provider_.service_directory_provider()->AddService(
        fake_color_transform_manager_.GetHandler());
  }

  void SetUp() final {
    // Create Scenic and Session.
    fuchsia::ui::scenic::SessionPtr session_ptr;
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
    fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
        listener_handle.NewRequest();
    fake_scenic_.CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
    fake_session_ = fake_scenic_.fakeSession();
    session_ =
        std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  }

  void TearDown() override {
    color_transform_handler_.reset();
    fake_session_ = nullptr;
  }

  sys::testing::ComponentContextProvider context_provider_;

  std::unique_ptr<scenic::Session> session_;
  FakeSession* fake_session_ = nullptr;  // Owned by fake_scenic_.
  FakeScenic fake_scenic_;
  FakeColorTransformManager fake_color_transform_manager_;
  std::unique_ptr<ColorTransformHandler> color_transform_handler_;
};
// Basic test to make sure the color transform handler can send updates to Scenic.
TEST_F(ColorTransformHandlerTest, VerifyA11yColorTransform) {
  // Create ColorTransformHandler.
  color_transform_handler_ =
      std::make_unique<ColorTransformHandler>(*context_provider_.context(), id, session_.get());
  RunLoopUntilIdle();

  // Change settings.
  fuchsia::accessibility::ColorTransformConfiguration configuration;
  configuration.set_color_correction(
      fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY);
  configuration.set_color_inversion_enabled(false);
  configuration.set_color_adjustment_matrix(kCorrectDeuteranomaly);

  ASSERT_TRUE(configuration.has_color_adjustment_matrix());
  color_transform_handler_->SetColorTransformConfiguration(std::move(configuration), [] {});
  RunLoopUntilIdle();

  // Verify that fake scenic received the correct matrix.
  ASSERT_TRUE(fake_session_->PresentWasCalled());
  auto command = fake_session_->GetFirstCommand();
  ASSERT_TRUE(command.has_value());
  ASSERT_EQ(command.value().Which(), fuchsia::ui::gfx::Command::Tag::kSetDisplayColorConversion);
  ASSERT_EQ(kCorrectDeuteranomaly, command.value().set_display_color_conversion().matrix);
}

// Verify that we don't call scenic when the accessibility matrix is missing.
TEST_F(ColorTransformHandlerTest, A11yMissingMatrix) {
  // Create ColorTransformHandler.
  color_transform_handler_ =
      std::make_unique<ColorTransformHandler>(*context_provider_.context(), id, session_.get());
  RunLoopUntilIdle();

  // Change settings.
  fuchsia::accessibility::ColorTransformConfiguration configuration;
  configuration.set_color_correction(
      fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY);
  configuration.set_color_inversion_enabled(false);
  EXPECT_FALSE(configuration.has_color_adjustment_matrix());

  color_transform_handler_->SetColorTransformConfiguration(std::move(configuration), [] {});
  RunLoopUntilIdle();

  // Verify that fake scenic was not called.
  ASSERT_FALSE(fake_session_->PresentWasCalled());
}

// Verify that a color adjustment from the brightness API is sent to scenic correctly.
TEST_F(ColorTransformHandlerTest, VerifyColorAdjustment) {
  // Create ColorTransformHandler.
  color_transform_handler_ =
      std::make_unique<ColorTransformHandler>(*context_provider_.context(), id, session_.get());
  RunLoopUntilIdle();

  // Change color adjustment via brightness.
  fuchsia::ui::brightness::ColorAdjustmentTable table;
  table.set_matrix(kTint);

  ASSERT_TRUE(table.has_matrix());
  color_transform_handler_->SetColorAdjustment(std::move(table));
  RunLoopUntilIdle();

  // Verify that fake scenic received the correct matrix.
  ASSERT_TRUE(fake_session_->PresentWasCalled());
  auto command = fake_session_->GetFirstCommand();
  ASSERT_TRUE(command.has_value());
  ASSERT_EQ(command.value().Which(), fuchsia::ui::gfx::Command::Tag::kSetDisplayColorConversion);
}

// Verify that a color adjustment from the brightness API is not sent to scenic when accessibility
// is active.
TEST_F(ColorTransformHandlerTest, VerifyColorAdjustmentNoOpWithA11y) {
  // Create ColorTransformHandler.
  color_transform_handler_ = std::make_unique<ColorTransformHandler>(
      *context_provider_.context(), id, session_.get(),
      ColorTransformState(/* color_inversion_enabled */ false,
                          fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY));
  RunLoopUntilIdle();

  // Change color adjustment via brightness.
  fuchsia::ui::brightness::ColorAdjustmentTable table;
  table.set_matrix(kTint);

  ASSERT_TRUE(table.has_matrix());
  color_transform_handler_->SetColorAdjustment(std::move(table));
  RunLoopUntilIdle();

  // Verify that fake scenic was not called.
  ASSERT_FALSE(fake_session_->PresentWasCalled());
}
// Verify that we don't call scenic when the brightness color adjustment matrix is not present.
TEST_F(ColorTransformHandlerTest, BrightnessMissingMatrix) {
  // Create ColorTransformHandler.
  color_transform_handler_ =
      std::make_unique<ColorTransformHandler>(*context_provider_.context(), id, session_.get());
  RunLoopUntilIdle();

  // Change color adjustment via brightness.
  fuchsia::ui::brightness::ColorAdjustmentTable table;
  ASSERT_FALSE(table.has_matrix());
  color_transform_handler_->SetColorAdjustment(std::move(table));
  RunLoopUntilIdle();

  // Verify that fake scenic was not called.
  ASSERT_FALSE(fake_session_->PresentWasCalled());
}

}  // namespace testing
}  // namespace root_presenter
