// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/a11y_settings_watcher.h"

#include <zircon/status.h>

#include <src/lib/fxl/logging.h>

namespace root_presenter {

const std::array<float, 3> kColorAdjustmentPostoffsets = {0, 0, 0};
const std::array<float, 3> kColorAdjustmentPreoffsets = {0, 0, 0};
const float kDefaultMagnificationZoomFactor = 1.0;

A11ySettingsWatcher::A11ySettingsWatcher(component::StartupContext* startup_context,
                                         scenic::ResourceId compositor_id, scenic::Session* session)
    : session_(session), compositor_id_(compositor_id), settings_watcher_bindings_(this) {
  FXL_DCHECK(startup_context);
  startup_context->ConnectToEnvironmentService(a11y_settings_manager_.NewRequest());
  a11y_settings_manager_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Unable to connect to A11y Settings Manager." << zx_status_get_string(status);
  });
  fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher_handle;
  settings_watcher_bindings_.Bind(watcher_handle.NewRequest());
  a11y_settings_manager_->Watch(std::move(watcher_handle));
}

void A11ySettingsWatcher::OnSettingsChange(fuchsia::accessibility::Settings new_settings) {
  if (HasColorConversionChanged(new_settings)) {
    // Create scenic color adjustment cmd.
    fuchsia::ui::gfx::Command color_adjustment_cmd;
    fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK display_color_conversion_cmd;
    InitColorConversionCmd(&display_color_conversion_cmd, new_settings);
    // Call scenic to apply color adjustment.
    color_adjustment_cmd.set_set_display_color_conversion(std::move(display_color_conversion_cmd));
    session_->Enqueue(std::move(color_adjustment_cmd));
    session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
  }

  SaveSettings(new_settings);
}

void A11ySettingsWatcher::InitColorConversionCmd(
    fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd,
    const fuchsia::accessibility::Settings& new_settings) {
  display_color_conversion_cmd->compositor_id = compositor_id_;
  display_color_conversion_cmd->preoffsets = kColorAdjustmentPreoffsets;
  if (new_settings.has_color_adjustment_matrix()) {
    display_color_conversion_cmd->matrix = new_settings.color_adjustment_matrix();
  }
  display_color_conversion_cmd->postoffsets = kColorAdjustmentPostoffsets;
}

void A11ySettingsWatcher::SaveSettings(const fuchsia::accessibility::Settings& new_settings) {
  settings_.set_magnification_enabled(new_settings.has_magnification_enabled() &&
                                      new_settings.magnification_enabled());

  if (new_settings.has_magnification_zoom_factor()) {
    settings_.set_magnification_zoom_factor(new_settings.magnification_zoom_factor());
  } else {
    settings_.set_magnification_zoom_factor(kDefaultMagnificationZoomFactor);
  }

  settings_.set_screen_reader_enabled(new_settings.has_screen_reader_enabled() &&
                                      new_settings.screen_reader_enabled());

  settings_.set_color_inversion_enabled(new_settings.has_color_inversion_enabled() &&
                                        new_settings.color_inversion_enabled());

  if (new_settings.has_color_correction()) {
    settings_.set_color_correction(new_settings.color_correction());
  } else {
    settings_.set_color_correction(fuchsia::accessibility::ColorCorrection::DISABLED);
  }

  if (new_settings.has_color_adjustment_matrix()) {
    settings_.set_color_adjustment_matrix(new_settings.color_adjustment_matrix());
  }
}

bool A11ySettingsWatcher::HasColorConversionChanged(
    const fuchsia::accessibility::Settings& new_settings) {
  fuchsia::accessibility::ColorCorrection old_color_correction =
      settings_.has_color_correction() ? settings_.color_correction()
                                       : fuchsia::accessibility::ColorCorrection::DISABLED;

  fuchsia::accessibility::ColorCorrection new_color_correction =
      new_settings.has_color_correction() ? new_settings.color_correction()
                                          : fuchsia::accessibility::ColorCorrection::DISABLED;

  bool old_color_inversion =
      settings_.has_color_inversion_enabled() && settings_.color_inversion_enabled();
  bool new_color_inversion =
      new_settings.has_color_inversion_enabled() && new_settings.color_inversion_enabled();

  // Check if Color Correction has changed.
  if (old_color_correction != new_color_correction) {
    return true;
  }

  // Check if Color Inversion has changed.
  if (old_color_inversion != new_color_inversion) {
    return true;
  }

  return false;
}

}  // namespace root_presenter
