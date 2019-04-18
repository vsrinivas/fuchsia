// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_MANAGER_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_MANAGER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <math.h>
#include <src/lib/fxl/macros.h>

// DEPRECATED

namespace a11y_manager {

class SettingsManagerImpl : public fuchsia::accessibility::SettingsManager {
 public:
  SettingsManagerImpl();
  ~SettingsManagerImpl() = default;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request);

  void GetSettings(GetSettingsCallback callback) override;
  void SetMagnificationEnabled(
      bool magnification_enabled,
      SetMagnificationEnabledCallback callback) override;
  void SetMagnificationZoomFactor(
      float_t magnification_zoom_factor,
      SetMagnificationZoomFactorCallback callback) override;
  void SetScreenReaderEnabled(bool screen_reader_enabled,
                              SetScreenReaderEnabledCallback callback) override;
  void SetColorInversionEnabled(
      bool color_inversion_enabled,
      SetColorInversionEnabledCallback callback) override;
  void SetColorCorrection(
      fuchsia::accessibility::ColorCorrection color_correction,
      SetColorCorrectionCallback callback) override;
  void Watch(fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher>
                 watcher) override;

 private:
  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::accessibility::SettingsWatcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const fuchsia::accessibility::Settings& new_settings);

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::accessibility::SettingsManager> bindings_;

  fuchsia::accessibility::Settings settings_;

  std::vector<fuchsia::accessibility::SettingsWatcherPtr> watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingsManagerImpl);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_MANAGER_H_
