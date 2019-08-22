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

void A11ySettingsWatcher::OnSettingsChange(fuchsia::accessibility::Settings settings) {
  SaveSettings(settings);

  // Create scenic color adjustment cmd.
  fuchsia::ui::gfx::Command color_adjustment_cmd;
  fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK display_color_conversion_cmd;
  InitColorConversionCmd(&display_color_conversion_cmd);
  // Call scenic to apply color adjustment.
  color_adjustment_cmd.set_set_display_color_conversion(std::move(display_color_conversion_cmd));
  session_->Enqueue(std::move(color_adjustment_cmd));
  session_->Present(0, [](fuchsia::images::PresentationInfo info) {});
}

void A11ySettingsWatcher::InitColorConversionCmd(
    fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd) {
  display_color_conversion_cmd->compositor_id = compositor_id_;
  display_color_conversion_cmd->preoffsets = kColorAdjustmentPreoffsets;
  display_color_conversion_cmd->matrix = settings_.color_adjustment_matrix();
  display_color_conversion_cmd->postoffsets = kColorAdjustmentPostoffsets;
}

void A11ySettingsWatcher::SaveSettings(const fuchsia::accessibility::Settings& provided_settings) {
  settings_.set_magnification_enabled(provided_settings.has_magnification_enabled() &&
                                      provided_settings.magnification_enabled());

  if (provided_settings.has_magnification_zoom_factor()) {
    settings_.set_magnification_zoom_factor(provided_settings.magnification_zoom_factor());
  } else {
    settings_.set_magnification_zoom_factor(kDefaultMagnificationZoomFactor);
  }

  settings_.set_screen_reader_enabled(provided_settings.has_screen_reader_enabled() &&
                                      provided_settings.screen_reader_enabled());

  settings_.set_color_inversion_enabled(provided_settings.has_color_inversion_enabled() &&
                                        provided_settings.color_inversion_enabled());

  if (provided_settings.has_color_correction()) {
    settings_.set_color_correction(provided_settings.color_correction());
  } else {
    settings_.set_color_correction(fuchsia::accessibility::ColorCorrection::DISABLED);
  }

  if (provided_settings.has_color_adjustment_matrix()) {
    settings_.set_color_adjustment_matrix(provided_settings.color_adjustment_matrix());
  }
}

}  // namespace root_presenter
