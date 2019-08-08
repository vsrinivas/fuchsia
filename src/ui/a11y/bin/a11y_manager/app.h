// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/bin/a11y_manager/semantics/semantics_manager_impl.h"
#include "src/ui/a11y/bin/a11y_manager/settings/settings_manager_impl.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

namespace a11y_manager {

// A11y manager application entry point.
class App : public fuchsia::accessibility::SettingsWatcher {
 public:
  explicit App(std::unique_ptr<sys::ComponentContext> context);
  ~App() = default;

  // |fuchsia::accessibility::SettingsWatcher|
  void OnSettingsChange(fuchsia::accessibility::Settings provided_settings) override;

  // Returns copy of current set of settings owned by A11y Manager.
  fuchsia::accessibility::SettingsPtr GetSettings();

 private:
  // Initialize function for the App.
  void Initialize();

  // Helper function to copy given settings to member variable.
  void SetSettings(fuchsia::accessibility::Settings provided_settings);

  // Initializes Screen Reader pointer when screen reader is enabled, and destroys
  // the pointer when Screen Reader is disabled.
  void OnScreenReaderEnabled(bool enabled);

  std::unique_ptr<sys::ComponentContext> startup_context_;

  // Pointer to Settings Manager Implementation.
  std::unique_ptr<SettingsManagerImpl> settings_manager_impl_;
  // Pointer to Semantics Manager Implementation.
  std::unique_ptr<SemanticsManagerImpl> semantics_manager_impl_;

  fidl::BindingSet<fuchsia::accessibility::SettingsWatcher> settings_watcher_bindings_;

  // Private variable for storing A11y Settings.
  fuchsia::accessibility::Settings settings_;

  // Pointer to SettingsManager service, which will be used for connecting App
  // to settings manager as a Watcher.
  fuchsia::accessibility::SettingsManagerPtr settings_manager_;

  std::unique_ptr<a11y::ScreenReader> screen_reader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
