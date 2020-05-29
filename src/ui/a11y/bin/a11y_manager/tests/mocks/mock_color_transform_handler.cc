// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "mock_color_transform_handler.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
namespace accessibility_test {

const std::array<float, 9> kIdentityMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
const std::array<float, 3> kZero = {0, 0, 0};

MockColorTransformHandler::MockColorTransformHandler(
    sys::testing::ComponentContextProvider* context)
    : color_inversion_enabled_(false),
      color_correction_mode_(fuchsia::accessibility::ColorCorrectionMode::DISABLED),
      transform_(kIdentityMatrix) {
  context->ConnectToPublicService(color_transform_ptr_.NewRequest());
  color_transform_ptr_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to ColorTransform with status:" << status;
  });
  fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> interface_handle;
  bindings_.AddBinding(this, interface_handle.NewRequest());
  color_transform_ptr_->RegisterColorTransformHandler(std::move(interface_handle));
}

MockColorTransformHandler::~MockColorTransformHandler() = default;

void MockColorTransformHandler::SetColorTransformConfiguration(
    fuchsia::accessibility::ColorTransformConfiguration configuration,
    SetColorTransformConfigurationCallback callback) {
  transform_ = configuration.has_color_adjustment_matrix() ? configuration.color_adjustment_matrix()
                                                           : kIdentityMatrix;
  pre_offset_ = configuration.has_color_adjustment_pre_offset()
                    ? configuration.color_adjustment_pre_offset()
                    : kZero;
  post_offset_ = configuration.has_color_adjustment_post_offset()
                     ? configuration.color_adjustment_post_offset()
                     : kZero;

  color_inversion_enabled_ =
      configuration.has_color_inversion_enabled() ? configuration.color_inversion_enabled() : false;

  color_correction_mode_ = configuration.has_color_correction()
                               ? configuration.color_correction()
                               : fuchsia::accessibility::ColorCorrectionMode::DISABLED;
  callback();
}

}  // namespace accessibility_test
