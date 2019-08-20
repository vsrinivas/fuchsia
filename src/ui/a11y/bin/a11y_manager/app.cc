// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace a11y_manager {

const float kDefaultMagnificationZoomFactor = 1.0;

App::App(std::unique_ptr<sys::ComponentContext> context)
    : startup_context_(std::move(context)),
      settings_manager_(std::make_unique<a11y::SettingsManagerImpl>()),
      semantics_manager_(std::make_unique<a11y::SemanticsManagerImpl>()),
      // TtsManager publishes the services it offers upon initialization.
      tts_manager_(std::make_unique<a11y::TtsManager>(startup_context_.get())) {
  Initialize();
}

void App::Initialize() {
  // Add Settings Manager service.
  startup_context_->outgoing()->AddPublicService<fuchsia::accessibility::SettingsManager>(
      [this](fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
        settings_manager_->AddBinding(std::move(request));
      });

  // Add Semantics Manager service.
  semantics_manager_->SetDebugDirectory(startup_context_->outgoing()->debug_dir());
  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::semantics::SemanticsManager>(
          [this](
              fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager> request) {
            semantics_manager_->AddBinding(std::move(request));
          });

  // Connect to Settings manager service and register a watcher.
  settings_manager_->AddBinding(settings_manager_ptr_.NewRequest());
  settings_manager_ptr_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Cannot connect to SettingsManager with status:"
                   << zx_status_get_string(status);
  });
  fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher_handle;
  settings_watcher_bindings_.AddBinding(this, watcher_handle.NewRequest());
  settings_manager_ptr_->Watch(std::move(watcher_handle));

  // For now, we use a simple Tts Engine which only logs the output.
  // On initialization, it registers itself with the Tts manager.
  log_engine_ = std::make_unique<a11y::LogEngine>(startup_context_.get());
}

fuchsia::accessibility::SettingsPtr App::GetSettings() {
  auto settings_ptr = fuchsia::accessibility::Settings::New();
  settings_.Clone(settings_ptr.get());
  return settings_ptr;
}

void App::SetSettings(fuchsia::accessibility::Settings provided_settings) {
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

void App::OnScreenReaderEnabled(bool enabled) {
  // Reset SemanticsTree and registered views in SemanticsManagerImpl.
  semantics_manager_->SetSemanticsManagerEnabled(enabled);

  // Reset ScreenReader.
  if (enabled) {
    screen_reader_ = std::make_unique<a11y::ScreenReader>();
  } else {
    screen_reader_.reset();
  }
}

void App::OnSettingsChange(fuchsia::accessibility::Settings provided_settings) {
  // Check if screen reader settings have changed.
  if ((settings_.has_screen_reader_enabled() && settings_.screen_reader_enabled()) !=
      (provided_settings.has_screen_reader_enabled() &&
       provided_settings.screen_reader_enabled())) {
    OnScreenReaderEnabled(provided_settings.screen_reader_enabled());
  }

  // Set A11y Settings.
  SetSettings(std::move(provided_settings));
}

}  // namespace a11y_manager
