// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color_transform_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/ui/a11y/lib/util/util.h"
namespace a11y {

// clang-format off
const std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};
const std::array<float, 3> kZero3x1Vector = {0, 0, 0};

// To invert a color vector in RGB space, we first convert it to
// YIQ color space, then rotate it along Y axis for 180 degrees,
// convert it back to RGB space, and subtract it by 1.
//
// Formula of inverted color:
//   [R' G' B']' = [1, 1, 1] - inv(T) . diag(1, -1, -1) . T . [R G B]'
//               = [1, 1, 1] + kColorInversionMatrix . [R G B]'
//
// where R, G, B \in [0, 1], and T is the RGB to YIQ conversion
// matrix:
//   T = [[0.299   0.587   0.114]
//        [0.596  -0.274  -0.321]
//        [0.211  -0.523   0.311]]
//
// Thus the color inveresion matrix is
//   kColorInversionMatrix
//    = [[ 0.402  -1.174  -0.228]
//       [-0.598  -0.174  -0.228]
//       [-0.599  -1.177   0.771]]
//
const std::array<float, 9> kColorInversionMatrix = {
    0.402f,  -1.174f,  -0.228f,
   -0.598f,  -0.174f,  -0.228f,
   -0.599f,  -1.177f,   0.771f};

// Post offsets should be strictly less than 1.
const std::array<float, 3> kColorInversionPostOffset = {.999f, .999f, .999f};

const std::array<float, 9> kCorrectProtanomaly = {
    0.622774, 0.264275,  0.216821,
    0.377226, 0.735725,  -0.216821,
    0.000000, -0.000000, 1.000000};
const std::array<float, 9> kCorrectDeuteranomaly = {
    0.288299f, 0.052709f,  -0.257912f,
    0.711701f, 0.947291f,  0.257912f,
    0.000000f, -0.000000f, 1.000000f};
const std::array<float, 9> kCorrectTritanomaly = {
    1.000000f,  0.000000f, -0.000000f,
    -0.805712f, 0.378838f, 0.104823f,
    0.805712f,  0.621162f, 0.895177f};
// clang-format on

namespace {

struct ColorAdjustmentArgs {
  std::array<float, 9> color_adjustment_matrix;
  std::array<float, 3> color_adjustment_pre_offset;
  std::array<float, 3> color_adjustment_post_offset;
};

ColorAdjustmentArgs GetColorAdjustmentArgs(
    bool color_inversion_enabled,
    fuchsia::accessibility::ColorCorrectionMode color_correction_mode) {
  std::array<float, 9> color_inversion_matrix = kIdentityMatrix;
  std::array<float, 9> color_correction_matrix = kIdentityMatrix;
  std::array<float, 3> color_adjustment_pre_offset = kZero3x1Vector;
  std::array<float, 3> color_adjustment_post_offset = kZero3x1Vector;

  if (color_inversion_enabled) {
    color_inversion_matrix = kColorInversionMatrix;
    color_adjustment_post_offset = kColorInversionPostOffset;
  }

  switch (color_correction_mode) {
    case fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY:
      color_correction_matrix = kCorrectProtanomaly;
      break;
    case fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY:
      color_correction_matrix = kCorrectDeuteranomaly;
      break;
    case fuchsia::accessibility::ColorCorrectionMode::CORRECT_TRITANOMALY:
      color_correction_matrix = kCorrectTritanomaly;
      break;
    case fuchsia::accessibility::ColorCorrectionMode::DISABLED:
      // fall through
    default:
      color_correction_matrix = kIdentityMatrix;
      break;
  }

  return ColorAdjustmentArgs{.color_adjustment_matrix = Multiply3x3MatrixRowMajor(
                                 color_inversion_matrix, color_correction_matrix),
                             .color_adjustment_pre_offset = color_adjustment_pre_offset,
                             .color_adjustment_post_offset = color_adjustment_post_offset};
}

}  // namespace

ColorTransformManager::ColorTransformManager(sys::ComponentContext* startup_context) {
  FX_CHECK(startup_context);
  startup_context->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void ColorTransformManager::RegisterColorTransformHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> handle) {
  color_transform_handler_ptr_ = handle.Bind();
  color_transform_handler_ptr_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "ColorTransformHandler disconnected with status: "
                   << zx_status_get_string(status);
  });
}

void ColorTransformManager::ChangeColorTransform(
    bool color_inversion_enabled,
    fuchsia::accessibility::ColorCorrectionMode color_correction_mode) {
  fuchsia::accessibility::ColorTransformConfiguration color_transform_configuration;
  ColorAdjustmentArgs color_adjustment_args =
      GetColorAdjustmentArgs(color_inversion_enabled, color_correction_mode);
  color_transform_configuration.set_color_inversion_enabled(color_inversion_enabled);
  color_transform_configuration.set_color_correction(color_correction_mode);
  color_transform_configuration.set_color_adjustment_matrix(
      color_adjustment_args.color_adjustment_matrix);
  color_transform_configuration.set_color_adjustment_post_offset(
      color_adjustment_args.color_adjustment_post_offset);
  color_transform_configuration.set_color_adjustment_pre_offset(
      color_adjustment_args.color_adjustment_pre_offset);

  if (!color_transform_handler_ptr_)
    return;

  color_transform_handler_ptr_->SetColorTransformConfiguration(
      std::move(color_transform_configuration),
      [] { FX_LOGS(INFO) << "Color transform configuration changed."; });
}

};  // namespace a11y
