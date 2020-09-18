// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

namespace a11y_manager {

const float kDefaultMagnificationZoomFactor = 1.0;

App::App(sys::ComponentContext* context, a11y::ViewManager* view_manager,
         a11y::TtsManager* tts_manager, a11y::ColorTransformManager* color_transform_manager,
         a11y::GestureListenerRegistry* gesture_listener_registry, inspect::Node inspect_node)
    : view_manager_(view_manager),
      tts_manager_(tts_manager),
      color_transform_manager_(color_transform_manager),
      gesture_listener_registry_(gesture_listener_registry),
      inspect_node_(std::move(inspect_node)) {
  FX_DCHECK(context);
  FX_DCHECK(view_manager);
  FX_DCHECK(tts_manager);
  FX_DCHECK(color_transform_manager);
  FX_DCHECK(gesture_listener_registry_);

  context->outgoing()->AddPublicService(semantics_manager_bindings_.GetHandler(view_manager_));
  context->outgoing()->AddPublicService(magnifier_bindings_.GetHandler(&magnifier_));
  context->outgoing()->AddPublicService(
      gesture_listener_registry_bindings_.GetHandler(gesture_listener_registry_));

  // Connect to Root presenter service.
  pointer_event_registry_ =
      context->svc()->Connect<fuchsia::ui::input::accessibility::PointerEventRegistry>();
  pointer_event_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::input::accessibility::PointerEventRegistry"
                   << zx_status_get_string(status);
  });

  // Inits Focus Chain focuser support / listening Focus Chain updates.
  focuser_registry_ = context->svc()->Connect<fuchsia::ui::views::accessibility::FocuserRegistry>();
  focuser_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::views::accessibility::FocuserRegistry"
                   << zx_status_get_string(status);
  });
  fuchsia::ui::views::FocuserPtr focuser;
  focuser_registry_->RegisterFocuser(focuser.NewRequest());
  focus_chain_manager_ =
      std::make_unique<a11y::FocusChainManager>(std::move(focuser), view_manager_);

  // |focus_chain_manager_| listens for Focus Chain updates. Connects to the listener registry and
  // start listening.
  focus_chain_listener_registry_ =
      context->svc()->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::focus::FocusChainListenerRegistry"
                   << zx_status_get_string(status);
  });
  auto focus_chain_listener_handle =
      focus_chain_listener_bindings_.AddBinding(focus_chain_manager_.get());
  focus_chain_listener_registry_->Register(focus_chain_listener_handle.Bind());

  // Connect to setui.
  setui_settings_ = context->svc()->Connect<fuchsia::settings::Accessibility>();
  setui_settings_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::settings::Accessibility" << zx_status_get_string(status);
  });

  // Connects to property provider to retrieve the current locale. Also adds a handler for the event
  // to process when the locale changes.
  property_provider_ = context->svc()->Connect<fuchsia::intl::PropertyProvider>();
  property_provider_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::intl::PropertyProvider" << zx_status_get_string(status);
    if (status == ZX_ERR_PEER_CLOSED) {
      FX_LOGS(ERROR) << "Using the default locale: en-US";
      fuchsia::intl::Profile default_profile;
      this->i18n_profile_ = std::move(default_profile);
      this->i18n_profile_->mutable_locales()->push_back({.id = "en-US"});
      if (!is_initialized_) {
        FinishSetUp();
      }
    }
  });
  property_provider_.events().OnChange =
      fit::bind_member(this, &App::PropertyProviderOnChangeHandler);
  // Fetches the initial locale.
  // When the locale is returned, marks this object as initialized and ready to process requests.
  // This is necessary because the Locale is a must-have information that needs to be present to
  // build some elements.
  property_provider_->GetProfile([this](fuchsia::intl::Profile profile) mutable {
    this->i18n_profile_ = std::move(profile);
    // Note: FinishSetUp() is invoked here because having the locale is the only must-have
    // information at the moment we need before we can proceed with regular flow. This should be
    // moved elsewhere if ever comes a new condition that needs to be met to consider this object
    // fully-initialized.
    FinishSetUp();
  });
}

App::~App() = default;

void App::FinishSetUp() {
  FX_DCHECK(i18n_profile_) << "App is being initialized without i18n profile from user.";
  // Start watching setui for current settings
  WatchSetui();
  is_initialized_ = true;
}

void App::SetState(A11yManagerState state) {
  state_ = state;

  UpdateScreenReaderState();
  UpdateMagnifierState();
  UpdateColorTransformState();
  // May rely on screen reader existence.
  UpdateGestureManagerState();
}

void App::UpdateScreenReaderState() {
  // If this is used elsewhere, it should be moved into its own function.
  view_manager_->SetSemanticsEnabled(state_.screen_reader_enabled());
  view_manager_->SetAnnotationsEnabled(state_.screen_reader_enabled());

  if (state_.screen_reader_enabled()) {
    if (!screen_reader_) {
      screen_reader_ = InitializeScreenReader();
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

void App::UpdateColorTransformState() {
  bool color_inversion = state_.color_inversion_enabled();
  fuchsia::accessibility::ColorCorrectionMode color_blindness_type = state_.color_correction_mode();
  color_transform_manager_->ChangeColorTransform(color_inversion, color_blindness_type);
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

bool App::GestureState::operator==(GestureState o) const {
  return screen_reader_gestures == o.screen_reader_gestures &&
         magnifier_gestures == o.magnifier_gestures;
}

void App::SetuiWatchCallback(fuchsia::settings::AccessibilitySettings settings) {
  SetState(state_.withSettings(settings));
  WatchSetui();
}

void App::WatchSetui() { setui_settings_->Watch(fit::bind_member(this, &App::SetuiWatchCallback)); }

// Converts setui color blindess type to the relevant accessibility color correction mode.
fuchsia::accessibility::ColorCorrectionMode ConvertColorCorrection(
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

A11yManagerState A11yManagerState::withSettings(
    const fuchsia::settings::AccessibilitySettings& systemSettings) {
  A11yManagerState state = *this;

  if (systemSettings.has_screen_reader()) {
    state.screen_reader_enabled_ = systemSettings.screen_reader();
  }

  if (systemSettings.has_enable_magnification()) {
    state.magnifier_enabled_ = systemSettings.enable_magnification();
  }

  if (systemSettings.has_color_inversion()) {
    state.color_inversion_enabled_ = systemSettings.color_inversion();
  }

  if (systemSettings.has_color_correction()) {
    state.color_correction_mode_ = ConvertColorCorrection(systemSettings.color_correction());
  }

  return state;
}

std::unique_ptr<a11y::ScreenReader> App::InitializeScreenReader() {
  auto a11y_focus_manager = std::make_unique<a11y::A11yFocusManager>(
      focus_chain_manager_.get(), focus_chain_manager_.get(), view_manager_,
      inspect_node_.CreateChild("focus_manager"));
  std::string locale_id = "en-US";
  if (i18n_profile_ && i18n_profile_->has_locales() && !i18n_profile_->locales().empty()) {
    locale_id = i18n_profile_->locales()[0].id;
  }
  auto screen_reader_context = std::make_unique<a11y::ScreenReaderContext>(
      std::move(a11y_focus_manager), tts_manager_, locale_id);
  auto screen_reader = std::make_unique<a11y::ScreenReader>(
      std::move(screen_reader_context), view_manager_, gesture_listener_registry_);
  view_manager_->GetSemanticsEventManager()->Register(
      screen_reader->GetSemanticsEventListenerWeakPtr());
  return screen_reader;
}

void App::PropertyProviderOnChangeHandler() {
  property_provider_->GetProfile([this](fuchsia::intl::Profile profile) {
    this->i18n_profile_ = std::move(profile);
    if (state_.screen_reader_enabled()) {
      if (i18n_profile_ && i18n_profile_->has_locales() && !i18n_profile_->locales().empty()) {
        const auto& locale_id = i18n_profile_->locales()[0].id;
        screen_reader_->context()->set_locale_id(locale_id);
      }
    }
  });
}

}  // namespace a11y_manager
