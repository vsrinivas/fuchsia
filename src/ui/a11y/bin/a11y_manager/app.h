// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader.h"
#include "src/ui/a11y/lib/semantics/semantics_manager.h"
#include "src/ui/a11y/lib/settings/settings_manager.h"
#include "src/ui/a11y/lib/tts/log_engine.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace a11y_manager {

// A11y manager application entry point.
class App : public fuchsia::accessibility::SettingsWatcher {
 public:
  explicit App(std::unique_ptr<sys::ComponentContext> context);
  ~App() override;

  // |fuchsia::accessibility::SettingsWatcher|
  void OnSettingsChange(fuchsia::accessibility::Settings provided_settings) override;

  // Returns a copy of current set of settings owned by A11y Manager.
  fuchsia::accessibility::SettingsPtr GetSettings();

 private:
  void Initialize();

  // Helper function to copy given settings to member variable.
  void SetSettings(fuchsia::accessibility::Settings provided_settings);

  // Initializes Screen Reader pointer when screen reader is enabled, and destroys
  // the pointer when Screen Reader is disabled.
  void OnScreenReaderEnabled(bool enabled);

  std::unique_ptr<sys::ComponentContext> startup_context_;

  std::unique_ptr<a11y::ScreenReader> screen_reader_;
  a11y::SemanticsManager semantics_manager_;
  a11y::SettingsManager settings_manager_;
  a11y::TtsManager tts_manager_;

  // A simple Tts engine which logs output.
  a11y::LogEngine log_engine_;

  fidl::Binding<fuchsia::accessibility::SettingsWatcher> settings_watcher_binding_;

  fuchsia::accessibility::Settings settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_APP_H_
