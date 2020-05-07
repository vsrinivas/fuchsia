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
const std::array<float, 9> kColorInversionMatrix = {
    0.402,  -0.598, -0.599,
    -1.174, -0.174, -1.175,
    -0.228, -0.228, 0.772};
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

std::array<float, 9> GetColorAdjustmentMatrix(
    bool color_inversion_enabled,
    fuchsia::accessibility::ColorCorrectionMode color_correction_mode) {
  std::array<float, 9> color_inversion_matrix = kIdentityMatrix;
  std::array<float, 9> color_correction_matrix = kIdentityMatrix;

  if (color_inversion_enabled) {
    color_inversion_matrix = kColorInversionMatrix;
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

  return Multiply3x3MatrixRowMajor(color_inversion_matrix, color_correction_matrix);
}

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
  color_transform_configuration.set_color_inversion_enabled(color_inversion_enabled);
  color_transform_configuration.set_color_correction(color_correction_mode);
  color_transform_configuration.set_color_adjustment_matrix(
      GetColorAdjustmentMatrix(color_inversion_enabled, color_correction_mode));
  if (!color_transform_handler_ptr_)
    return;

  color_transform_handler_ptr_->SetColorTransformConfiguration(
      std::move(color_transform_configuration),
      [] { FX_LOGS(INFO) << "Color transform configuration changed."; });
}

};  // namespace a11y
