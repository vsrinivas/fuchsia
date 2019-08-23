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
      // TtsManager publishes the services it offers upon initialization.
      tts_manager_(startup_context_.get()),
      // For now, we use a simple Tts Engine which only logs the output.
      // On initialization, it registers itself with the Tts manager.
      log_engine_(startup_context_.get()),
      settings_watcher_binding_(this) {
  Initialize();
}

App::~App() = default;

void App::Initialize() {
  // Add Settings Manager service.
  startup_context_->outgoing()->AddPublicService<fuchsia::accessibility::SettingsManager>(
      [this](fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
        settings_manager_.AddBinding(std::move(request));
      });

  // Add Semantics Manager service.
  semantics_manager_.SetDebugDirectory(startup_context_->outgoing()->debug_dir());
  startup_context_->outgoing()
      ->AddPublicService<fuchsia::accessibility::semantics::SemanticsManager>(
          [this](
              fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager> request) {
            semantics_manager_.AddBinding(std::move(request));
          });

  // Register ourselves as a settings watcher.
  settings_manager_.Watch(settings_watcher_binding_.NewBinding());

  // Connect to Root presenter service.
  pointer_event_registry_ =
      startup_context_->svc()->Connect<fuchsia::ui::input::accessibility::PointerEventRegistry>();
  pointer_event_registry_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Cannot connect to PointerEventRegistry with status:"
                   << zx_status_get_string(status);
  });
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
  semantics_manager_.SetSemanticsManagerEnabled(enabled);

  // Reset ScreenReader.
  if (enabled) {
    screen_reader_ = std::make_unique<a11y::ScreenReader>();
  } else {
    screen_reader_.reset();
  }
}

void App::OnAccessibilityPointerEventListenerEnabled(bool enabled) {
  if (enabled) {
    gesture_manager_ = std::make_unique<a11y::GestureManager>();
    auto listener_handle = listener_bindings_.AddBinding(gesture_manager_.get());
    pointer_event_registry_->Register(std::move(listener_handle));
  } else {
    listener_bindings_.CloseAll();
    gesture_manager_.reset();
  }
}

void App::OnSettingsChange(fuchsia::accessibility::Settings provided_settings) {
  // Check if screen reader settings have changed.
  if (provided_settings.has_screen_reader_enabled()) {
    const bool screen_reader_enabled = provided_settings.screen_reader_enabled();
    if ((settings_.has_screen_reader_enabled() && settings_.screen_reader_enabled()) !=
        screen_reader_enabled) {
      OnAccessibilityPointerEventListenerEnabled(screen_reader_enabled);
      OnScreenReaderEnabled(screen_reader_enabled);
    }
  }
  // Set A11y Settings.
  SetSettings(std::move(provided_settings));
}

}  // namespace a11y_manager
