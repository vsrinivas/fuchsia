// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_COLOR_TRANSFORM_HANDLER_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_COLOR_TRANSFORM_HANDLER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/fxl/macros.h"
namespace accessibility_test {

class MockColorTransformHandler : public fuchsia::accessibility::ColorTransformHandler {
 public:
  MockColorTransformHandler(sys::testing::ComponentContextProvider* context);
  ~MockColorTransformHandler() override;

  // |fuchsia.accessibility.ColorTransformHandler|
  void SetColorTransformConfiguration(
      fuchsia::accessibility::ColorTransformConfiguration configuration,
      SetColorTransformConfigurationCallback callback) override;

  fuchsia::accessibility::ColorCorrectionMode GetColorCorrectionMode() {
    return color_correction_mode_;
  }
  bool GetColorInversionEnabled() { return color_inversion_enabled_; }

 private:
  fuchsia::accessibility::ColorTransformPtr color_transform_ptr_;
  fidl::BindingSet<fuchsia::accessibility::ColorTransformHandler> bindings_;
  bool color_inversion_enabled_;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;
  std::array<float, 9> transform_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockColorTransformHandler);
};
}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_COLOR_TRANSFORM_HANDLER_H_
