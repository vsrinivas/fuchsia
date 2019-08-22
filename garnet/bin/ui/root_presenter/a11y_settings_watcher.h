// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_A11Y_SETTINGS_WATCHER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_A11Y_SETTINGS_WATCHER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <memory>

namespace root_presenter {

class A11ySettingsWatcher : public fuchsia::accessibility::SettingsWatcher {
 public:
  explicit A11ySettingsWatcher(component::StartupContext* startup_context,
                               scenic::ResourceId compositor_id, scenic::Session* session);
  ~A11ySettingsWatcher() = default;

  // |fuchsia::accessibility::SettingsWatcher|
  void OnSettingsChange(fuchsia::accessibility::Settings settings) override;

  fuchsia::accessibility::SettingsPtr CloneA11ySettings() {
    fuchsia::accessibility::SettingsPtr settings_ptr = fuchsia::accessibility::Settings::New();
    settings_.Clone(settings_ptr.get());
    return settings_ptr;
  }

 private:
  void SaveSettings(const fuchsia::accessibility::Settings& provided_settings);
  void InitColorConversionCmd(
      fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd);

  scenic::Session* const session_;  // No ownership.
  scenic::ResourceId compositor_id_;
  fuchsia::accessibility::Settings settings_;
  fidl::Binding<fuchsia::accessibility::SettingsWatcher> settings_watcher_bindings_;
  fuchsia::accessibility::SettingsManagerPtr a11y_settings_manager_;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_A11Y_SETTINGS_WATCHER_H_
