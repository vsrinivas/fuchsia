// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <fuchsia/accessibility/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/configuration/color_transform_manager.h"
#include "src/ui/a11y/lib/focus_chain/focus_chain_manager.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/magnifier/magnifier_2.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/boot_info_manager.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace a11y_manager {

// Internal representation of the current state of the accessibility manager features.
// TODO(fxbug.dev/41768): Add in color correction and inversion
class A11yManagerState {
 public:
  // Default state with all values as disabled
  A11yManagerState()
      : color_correction_mode_(fuchsia::accessibility::ColorCorrectionMode::DISABLED) {}

  // Copy constructor
  A11yManagerState(const A11yManagerState& other) = default;
  A11yManagerState& operator=(const A11yManagerState& other) = default;

  bool screen_reader_enabled() const { return screen_reader_enabled_; }

  bool magnifier_enabled() const { return magnifier_enabled_; }

  bool color_inversion_enabled() const { return color_inversion_enabled_; }

  bool announce_screen_reader_enabled() const { return announce_screen_reader_enabled_; }
  void set_announce_screen_reader_enabled(bool announce_screen_reader_enabled) {
    announce_screen_reader_enabled_ = announce_screen_reader_enabled;
  }

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
  bool screen_reader_enabled_ = false;
  bool magnifier_enabled_ = false;
  bool color_inversion_enabled_ = false;
  // Indicates whether the screen reader should vocalize when initialized.
  bool announce_screen_reader_enabled_ = false;
  fuchsia::accessibility::ColorCorrectionMode color_correction_mode_;
};

// Represents the state of initialization of the a11y manager.
class A11yManagerInitializationState {
 public:
  using OnA11yManagerInitializedCallback = fit::function<void()>;

  // The default ocnstructor constructs a state not initialized.
  A11yManagerInitializationState() = default;
  ~A11yManagerInitializationState() = default;

  // Sets a callback to be invoked when tthis state is considered to be initialized.
  void SetOnA11yManagerInitializedCallback(OnA11yManagerInitializedCallback callback) {
    callback_ = std::move(callback);
  }

  // Returns true if A11y manager has all its dependencies initialized.
  bool IsInitialized() const { return has_i18N_profile_ && is_a11y_view_initialized_; }

  void SetI18nProfileReady();
  void SetA11yViewReady();

 private:
  OnA11yManagerInitializedCallback callback_;
  bool has_i18N_profile_ = false;
  bool is_a11y_view_initialized_ = false;
};

// A11y manager application entry point.
class App {
 public:
  // App dependencies which are trivial to set up and contribute to easier test should be
  // passed in the constructor.
  explicit App(sys::ComponentContext* context, a11y::ViewManager* view_manager,
               a11y::TtsManager* tts_manager, a11y::ColorTransformManager* color_transform_manager,
               a11y::GestureListenerRegistry* gesture_listener_registry,
               a11y::BootInfoManager* boot_info_manager,
               a11y::ScreenReaderContextFactory* screen_reader_context_factory,
               inspect::Node inspect_node = inspect::Node(), bool use_flatland = false);
  ~App();

  // Sets the a11y manager to the given configuration. Visible for testing.
  void SetState(A11yManagerState newState);
  A11yManagerState state() { return state_; }

  a11y::ScreenReader* screen_reader() { return screen_reader_.get(); }

  static constexpr char kIntlPropertyProviderDisconnectedInspectName[] =
      "intl_property_provider_disconnected";

 private:
  // If gesture manager/handler/arena ever get idempotent operations, we can remove this.

  struct GestureState {
    bool screen_reader_gestures = false;
    bool magnifier_gestures = false;

    bool has_any() const { return screen_reader_gestures || magnifier_gestures; }
    bool operator==(GestureState o) const;
  };

  // Finishes the SetUp of this object. For now, only the fetch of the current settings is called
  // here. If any condition needs this object to be fully-initialized (all class members are
  // initialized), they must be invoked here. For example: the field |profile_| is only available
  // after this object is initialized, and settings relly on this field to process its logic. Trying
  // to handle the settings logic without |profile_| would result in an error.
  void FinishSetUp();

  // Callback for Setui's Watch() method.
  void SetuiWatchCallback(fuchsia::settings::AccessibilitySettings settings);

  // Set up continuous watch of setui's accessibility settings. The Watch(...) method returns on
  // the initial call, and afterwards uses a hanging get to return only when settings change.
  void WatchSetui();

  void UpdateScreenReaderState();
  void UpdateMagnifierState();
  void UpdateColorTransformState();
  void UpdateGestureManagerState();

  // Initializes the Screen Reader, instantiating its context and related services.
  std::unique_ptr<a11y::ScreenReader> InitializeScreenReader();

  // |fuchsia.intl.PropertyProvider|
  // Handler invoked when the FIDL event OnChange is called.
  // Fetches the user's i18n profile and stores in |i18n_profile_|.
  void PropertyProviderOnChangeHandler();

  // True if we are using Flatland, false if we are using Gfx.
  // TODO(fxbug.dev/114627) Clean this up when Gfx is deleted.
  bool use_flatland_;

  // Data fields that must be initialized to consider this object as initialized.
  // Current state of the a11y manager
  A11yManagerState state_;
  A11yManagerInitializationState initialization_state_;

  // The user's i18n profile.
  std::optional<fuchsia::intl::Profile> i18n_profile_;
  // End of list of data fields that must be set to consider this object initialized.

  sys::ComponentContext* context_;

  std::unique_ptr<a11y::ScreenReader> screen_reader_;
  a11y::ViewManager* view_manager_;
  a11y::TtsManager* tts_manager_;
  a11y::ColorTransformManager* color_transform_manager_;
  a11y::GestureListenerRegistry* gesture_listener_registry_;
  a11y::ScreenReaderContextFactory* screen_reader_context_factory_;
  std::unique_ptr<a11y::FocusChainManager> focus_chain_manager_;
  // The gesture manager is instantiated whenever a11y manager starts listening
  // for pointer events, and destroyed when the listener disconnects.
  std::unique_ptr<a11y::GestureManager> gesture_manager_;
  GestureState gesture_state_;
  std::unique_ptr<a11y::Magnifier2> magnifier_;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> semantics_manager_bindings_;
  fidl::BindingSet<fuchsia::accessibility::virtualkeyboard::Registry>
      virtualkeyboard_registry_bindings_;

  fidl::BindingSet<fuchsia::ui::input::accessibility::PointerEventListener> listener_bindings_;
  fidl::BindingSet<fuchsia::ui::focus::FocusChainListener> focus_chain_listener_bindings_;

  fidl::BindingSet<fuchsia::accessibility::Magnifier> magnifier_bindings_;

  fidl::BindingSet<fuchsia::accessibility::gesture::ListenerRegistry>
      gesture_listener_registry_bindings_;

  // Interface between a11y manager and Scenic to register a
  // accessibility pointer event listener.
  fuchsia::ui::input::accessibility::PointerEventRegistryPtr pointer_event_registry_;

  // Interface between Setui and a11y manager to get updates when user settings change.
  fuchsia::settings::AccessibilityPtr setui_settings_;

  // Interface used to retrieve the current locale and watch when it changes.
  fuchsia::intl::PropertyProviderPtr property_provider_;

  // Inspect node to which to publish debug info.
  inspect::Node inspect_node_;

  // Inspect property indicating whether a locale was found.
  // If false, the a11y manager could not connect to the PropertyProvider, and
  // defaulted to the locale "en-US".
  inspect::BoolProperty inspect_property_intl_property_provider_disconnected_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
