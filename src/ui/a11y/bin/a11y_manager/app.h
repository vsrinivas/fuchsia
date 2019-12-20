// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/configuration/color_transform_manager.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/magnifier/magnifier.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader.h"
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
#include "src/ui/a11y/lib/tts/log_engine.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace a11y_manager {

// Internal representation of the current state of the accessibility manager features.
// TODO(fxb/41768): Add in color correction and inversion
class A11yManagerState {
 public:
  // Default state with all values as disabled
  A11yManagerState()
      : screen_reader_enabled_(false),
        magnifier_enabled_(false),
        color_inversion_enabled_(false),
        color_correction_mode_(fuchsia::accessibility::ColorCorrectionMode::DISABLED) {}

  // Copy constructor
  A11yManagerState(const A11yManagerState& other) = default;
  A11yManagerState& operator=(const A11yManagerState& other) = default;

  bool screen_reader_enabled() const { return screen_reader_enabled_; }

  bool magnifier_enabled() const { return magnifier_enabled_; }

  bool color_inversion_enabled() const { return color_inversion_enabled_; }

  fuchsia::accessibility::ColorCorrectionMode color_correction_mode() const {
    return color_correction_mode_;
  }

  A11yManagerState withScreenReaderEnabled(bool enabled) {
    A11yManagerState state = *this;
    state.screen_reader_enabled_ = enabled;
    return state;
  }

  A11yManagerState withMagnifierEnabled(bool enabled) {
    A11yManagerState state = *this;
    state.magnifier_enabled_ = enabled;
    return state;
  }

  // Creates a new instance of state that has any set values from the given AccessibilitySettings
  // applied.
  A11yManagerState withSettings(const fuchsia::settings::AccessibilitySettings& systemSettings);

 private:
  bool screen_reader_enabled_;
  bool magnifier_enabled_;
  bool color_inversion_enabled_;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;
};

// A11y manager application entry point.
class App {
 public:
  explicit App(std::unique_ptr<sys::ComponentContext> context);
  ~App();

  // Sets the a11y manager to the given configuration. Visible for testing.
  void SetState(A11yManagerState newState);
  A11yManagerState state() { return state_; };

 private:
  // If gesture manager/handler/arena ever get idempotent operations, we can remove this.

  struct GestureState {
    bool screen_reader_gestures = false;
    bool magnifier_gestures = false;

    bool has_any() const { return screen_reader_gestures || magnifier_gestures; }
    bool operator==(GestureState o) const;
  };

  // Callback for Setui's Watch() method.
  void SetuiWatchCallback(fuchsia::settings::Accessibility_Watch_Result result);

  // Set up continuous watch of setui's accessibility settings. The Watch(...) method returns on the
  // initial call, and afterwards uses a hanging get to return only when settings change.
  void WatchSetui();

  void UpdateScreenReaderState();
  void UpdateMagnifierState();
  void UpdateColorTransformState();
  void UpdateGestureManagerState();

  // Current state of the a11y manager
  A11yManagerState state_;

  std::unique_ptr<sys::ComponentContext> startup_context_;

  std::unique_ptr<a11y::ScreenReader> screen_reader_;
  a11y::SemanticsManager semantics_manager_;
  a11y::TtsManager tts_manager_;
  a11y::ColorTransformManager color_transform_manager_;
  // A simple Tts engine which logs output.
  a11y::LogEngine log_engine_;
  // The gesture manager is instantiated whenever a11y manager starts listening
  // for pointer events, and destroyed when the listener disconnects.
  std::unique_ptr<a11y::GestureManager> gesture_manager_;
  GestureState gesture_state_;
  a11y::Magnifier magnifier_;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> semantics_manager_bindings_;

  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventListener> listener_bindings_;

  fidl::BindingSet<fuchsia::accessibility::Magnifier> magnifier_bindings_;

  // Interface between a11y manager and Root presenter to register a
  // accessibility pointer event listener.
  fuchsia::ui::input::accessibility::PointerEventRegistryPtr pointer_event_registry_;

  // Interface between Setui and a11y manager to get updates when user settings change.
  fuchsia::settings::AccessibilityPtr setui_settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
