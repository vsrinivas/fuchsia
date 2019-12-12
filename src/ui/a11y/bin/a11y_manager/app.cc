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

  UpdateScreenReaderState();
  UpdateMagnifierState();

  // May rely on screen reader existence.
  UpdateGestureManagerState();
}

void App::UpdateScreenReaderState() {
  // If this is used elsewhere, it should be moved into its own function.
  semantics_manager_.SetSemanticsManagerEnabled(state_.screen_reader_enabled());

  if (state_.screen_reader_enabled()) {
    if (!screen_reader_) {
      screen_reader_ = std::make_unique<a11y::ScreenReader>(&semantics_manager_, &tts_manager_);
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
  GestureState new_state = {.screen_reader_gestures = state_.screen_reader_enabled(),
                            .magnifier_gestures = state_.magnifier_enabled()};

  if (new_state == gesture_state_)
    return;

  gesture_state_ = new_state;

  // For now the easiest way to properly set up all gestures with the right priorities is to rebuild
  // the gesture manager when the gestures change.

  if (!gesture_state_.has_any()) {
    // Shut down and clean up if no users
    gesture_manager_.reset();
  } else {
    gesture_manager_ = std::make_unique<a11y::GestureManager>();
    pointer_event_registry_->Register(gesture_manager_->binding().NewBinding());

    // The ordering of these recognizers is significant, as it signifies priority.

    if (gesture_state_.magnifier_gestures) {
      gesture_manager_->arena()->Add(&magnifier_);
    }

    if (gesture_state_.screen_reader_gestures) {
      screen_reader_->BindGestures(gesture_manager_->gesture_handler());
      gesture_manager_->gesture_handler()->ConsumeAll();
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

bool App::GestureState::operator==(GestureState o) const {
  return screen_reader_gestures == o.screen_reader_gestures &&
         magnifier_gestures == o.magnifier_gestures;
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
