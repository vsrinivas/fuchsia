// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/brightness/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <memory>

#include "src/ui/bin/root_presenter/safe_presenter.h"

namespace root_presenter {
class ColorTransformState {
 public:
  ColorTransformState()
      : color_inversion_enabled_(false),
        color_correction_mode_(fuchsia::accessibility::ColorCorrectionMode::DISABLED) {}
  ColorTransformState(bool color_inversion_enabled,
                      fuchsia::accessibility::ColorCorrectionMode mode)
      : color_inversion_enabled_(color_inversion_enabled), color_correction_mode_(mode) {}

  bool IsActive() {
    return color_inversion_enabled_ ||
           (color_correction_mode_ != fuchsia::accessibility::ColorCorrectionMode::DISABLED);
  }

  void Update(const fuchsia::accessibility::ColorTransformConfiguration configuration) {
    if (configuration.has_color_inversion_enabled()) {
      color_inversion_enabled_ = configuration.color_inversion_enabled();
    }

    if (configuration.has_color_correction()) {
      color_correction_mode_ = configuration.color_correction();
    }
  }

  bool color_inversion_enabled_ = false;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;
};

// Color Transform Handler is responsible for sending commands to the display to modify pixel
// data. This includes translating color transform requests into Scenic commands to change the
// display's color transform, tracking whether accessibility color correction is currently
// applied and applying clamping to the minimum allowed rgb value in order to adjust for
// backlight bleeding.
class ColorTransformHandler : public fuchsia::accessibility::ColorTransformHandler,
                              public fuchsia::ui::brightness::ColorAdjustmentHandler,
                              public fuchsia::ui::policy::DisplayBacklight {
 public:
  explicit ColorTransformHandler(sys::ComponentContext* component_context,
                                 scenic::ResourceId compositor_id, scenic::Session* session,
                                 SafePresenter* safe_presenter);

  explicit ColorTransformHandler(sys::ComponentContext* component_context,
                                 scenic::ResourceId compositor_id, scenic::Session* session,
                                 SafePresenter* safe_presenter, ColorTransformState state);

  ~ColorTransformHandler();

  // SetColorTransformConfiguration is called (typically by Accessibility Manager) to request a
  // change in color transform.
  // |fuchsia::accessibility::ColorTransformHandler|
  void SetColorTransformConfiguration(
      fuchsia::accessibility::ColorTransformConfiguration configuration,
      SetColorTransformConfigurationCallback callback) override;

  // SetColorAdjustment is called to tint the screen, typically by whatever component is responsible
  // for implementing the current UI. These changes will not be honored if accessibility color
  // correction is currently active.
  // |fuchsia::ui::brightness::ColorAdjustmentHandler|
  void SetColorAdjustment(
      fuchsia::ui::brightness::ColorAdjustmentTable color_adjustment_table) override;

  // SetMinimumRgb is called to clamp the minimum RGB value the display can take on. This is to
  // reduce the amount of backlighting seen on certain devices in dark environments. The clamping
  // value is passed along to the display controller regardless of what other ally color correction
  // or brightness correction is also being applied.
  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCallback callback) override;

 private:
  void SetScenicColorConversion(const std::array<float, 9> color_transform_matrix,
                                const std::array<float, 3> color_transform_pre_offsets,
                                const std::array<float, 3> color_transform_post_offsets);

  // Creates the scenic command to apply the requested change.
  void InitColorConversionCmd(
      fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd,
      const std::array<float, 9> color_transform_matrix,
      const std::array<float, 3> color_transform_pre_offsets,
      const std::array<float, 3> color_transform_post_offsets);

  bool color_transform_values_initialized_ = false;
  std::array<float, 9> prev_color_transform_matrix_;
  std::array<float, 3> prev_color_transform_pre_offsets_;
  std::array<float, 3> prev_color_transform_post_offsets_;

  sys::ComponentContext* const component_context_ = nullptr;
  scenic::Session* session_ = nullptr;  // No ownership.
  SafePresenter* safe_presenter_ = nullptr;
  const scenic::ResourceId compositor_id_;
  fidl::Binding<fuchsia::accessibility::ColorTransformHandler> color_transform_handler_bindings_;
  fidl::BindingSet<fuchsia::ui::brightness::ColorAdjustmentHandler> color_adjustment_bindings_;
  fidl::BindingSet<fuchsia::ui::policy::DisplayBacklight> display_backlight_bindings_;
  fuchsia::accessibility::ColorTransformPtr color_transform_manager_;
  ColorTransformState color_transform_state_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_
