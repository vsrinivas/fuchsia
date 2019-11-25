// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <zircon/status.h>

#include "src/lib/syslog/cpp/logger.h"

namespace a11y_manager {

const float kDefaultMagnificationZoomFactor = 1.0;

App::App(std::unique_ptr<sys::ComponentContext> context)
    : startup_context_(std::move(context)),
      // The following services publish themselves upon initialization.
      semantics_manager_(startup_context_.get()),
      tts_manager_(startup_context_.get()),
      // For now, we use a simple Tts Engine which only logs the output.
      // On initialization, it registers itself with the Tts manager.
      color_transform_manager_(startup_context_.get()),
      log_engine_(startup_context_.get()) {
  startup_context_->outgoing()->AddPublicService(
      settings_manager_bindings_.GetHandler(&settings_manager_));
  startup_context_->outgoing()->AddPublicService(magnifier_bindings_.GetHandler(&magnifier_));

  // Register a11y manager as a settings provider.
  settings_manager_.RegisterSettingProvider(settings_provider_ptr_.NewRequest());
  settings_provider_ptr_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::accessibility::settings::SettingsProvider"
                   << zx_status_get_string(status);
  });

  // Connect to Root presenter service.
  pointer_event_registry_ =
      startup_context_->svc()->Connect<fuchsia::ui::input::accessibility::PointerEventRegistry>();
  pointer_event_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::input::accessibility::PointerEventRegistry"
                   << zx_status_get_string(status);
  });

  // Connect to setui.
  setui_settings_ = startup_context_->svc()->Connect<fuchsia::settings::Accessibility>();
  setui_settings_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::settings::Accessibility" << zx_status_get_string(status);
  });

  // Start watching setui for current settings
  WatchSetui();
}

App::~App() = default;

void App::SetState(A11yManagerState state) {
  state_ = state;

  // Screen Reader and Magnifier depend on gesture manager state being correct.
  UpdateGestureManagerState();

  UpdateScreenReaderState();
  UpdateMagnifierState();
}

void App::UpdateScreenReaderState() {
  // If this is used elsewhere, it should be moved into its own function.
  semantics_manager_.SetSemanticsManagerEnabled(state_.screen_reader_enabled());

  if (state_.screen_reader_enabled()) {
    if (!screen_reader_) {
      // TODO:(fxb/41769) We should move more of the enable/disable logic outside of screen reader.
      screen_reader_ = std::make_unique<a11y::ScreenReader>(&semantics_manager_, &tts_manager_,
                                                            gesture_manager_.get());
    }
  } else {
    screen_reader_.reset();
  }
}

void App::UpdateMagnifierState() {
  if (!state_.magnifier_enabled()) {
    magnifier_.ZoomOutIfMagnified();
  }
}

void App::UpdateGestureManagerState() {
  bool no_active_users = !state_.magnifier_enabled() && !state_.screen_reader_enabled();

  if (no_active_users) {
    // Shut down and clean up if no users
    gesture_manager_.reset();
  } else {
    // Initialize if not initialized
    if (!gesture_manager_) {
      gesture_manager_ = std::make_unique<a11y::GestureManager>();
      pointer_event_registry_->Register(gesture_manager_->binding().NewBinding());
      gesture_manager_->arena()->Add(&magnifier_);
    }

    // Current logic for event handling policy is as follows:
    // Screen reader only: consume events
    // Screen reader and magnifier enabled: consume events
    // Just magnifier enabled: reject events
    if (state_.screen_reader_enabled()) {
      gesture_manager_->arena()->event_handling_policy(
          a11y::GestureArena::EventHandlingPolicy::kConsumeEvents);
    } else {
      gesture_manager_->arena()->event_handling_policy(
          a11y::GestureArena::EventHandlingPolicy::kRejectEvents);
    }
  }
}

void InternalSettingsCallback(fuchsia::accessibility::SettingsManagerStatus status) {
  if (status == fuchsia::accessibility::SettingsManagerStatus::ERROR) {
    FX_LOGS(ERROR) << "Error writing internal accessibility settings.";
  }
}

// This currently ignores errors in the internal settings API. That API is being removed in favor of
// smaller feature-oriented APIs.
void App::UpdateInternalSettings(const fuchsia::settings::AccessibilitySettings& systemSettings) {
  // New codepath for color transforms.
  bool color_inversion =
      systemSettings.has_color_inversion() ? systemSettings.color_inversion() : false;
  fuchsia::accessibility::ColorCorrectionMode color_blindness_type =
      systemSettings.has_color_correction()
          ? ConvertColorCorrection(systemSettings.color_correction())
          : fuchsia::accessibility::ColorCorrectionMode::DISABLED;
  color_transform_manager_.ChangeColorTransform(color_inversion, color_blindness_type);

  // Everything below here in this method is old code for  the legacy settings API.
  // TODO(17180): Remove this code when nothing else depends on it.
  if (systemSettings.has_color_inversion()) {
    settings_provider_ptr_->SetColorInversionEnabled(systemSettings.color_inversion(),
                                                     InternalSettingsCallback);
  }
  if (systemSettings.has_color_correction()) {
    switch (systemSettings.color_correction()) {
      case fuchsia::settings::ColorBlindnessType::NONE:
        settings_provider_ptr_->SetColorCorrection(
            fuchsia::accessibility::ColorCorrection::DISABLED, InternalSettingsCallback);
        break;
      case fuchsia::settings::ColorBlindnessType::PROTANOMALY:
        settings_provider_ptr_->SetColorCorrection(
            fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY, InternalSettingsCallback);
        break;
      case fuchsia::settings::ColorBlindnessType::DEUTERANOMALY:
        settings_provider_ptr_->SetColorCorrection(
            fuchsia::accessibility::ColorCorrection::CORRECT_DEUTERANOMALY,
            InternalSettingsCallback);
        break;
      case fuchsia::settings::ColorBlindnessType::TRITANOMALY:
        settings_provider_ptr_->SetColorCorrection(
            fuchsia::accessibility::ColorCorrection::CORRECT_TRITANOMALY, InternalSettingsCallback);
        break;
    }
  }
}

void App::SetuiWatchCallback(fuchsia::settings::Accessibility_Watch_Result result) {
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Error reading setui accessibility settings.";
  } else if (result.is_response()) {
    UpdateInternalSettings(result.response().settings);
    SetState(state_.withSettings(result.response().settings));
  }
  WatchSetui();
}

void App::WatchSetui() { setui_settings_->Watch(fit::bind_member(this, &App::SetuiWatchCallback)); }

fuchsia::accessibility::SettingsPtr App::GetSettings() const {
  return settings_manager_.GetSettings();
}

fuchsia::accessibility::ColorCorrectionMode App::ConvertColorCorrection(
    fuchsia::settings::ColorBlindnessType color_blindness_type) {
  switch (color_blindness_type) {
    case fuchsia::settings::ColorBlindnessType::PROTANOMALY:
      return fuchsia::accessibility::ColorCorrectionMode::CORRECT_PROTANOMALY;
    case fuchsia::settings::ColorBlindnessType::DEUTERANOMALY:
      return fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY;

    case fuchsia::settings::ColorBlindnessType::TRITANOMALY:
      return fuchsia::accessibility::ColorCorrectionMode::CORRECT_TRITANOMALY;
    case fuchsia::settings::ColorBlindnessType::NONE:
    // fall through
    default:
      return fuchsia::accessibility::ColorCorrectionMode::DISABLED;
  }
}

}  // namespace a11y_manager
