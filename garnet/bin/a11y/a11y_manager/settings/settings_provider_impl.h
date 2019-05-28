// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_PROVIDER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_PROVIDER_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <math.h>
#include <src/lib/fxl/macros.h>

#include <array>

namespace a11y_manager {

class SettingsProviderImpl : public fuchsia::accessibility::SettingsProvider {
 public:
  explicit SettingsProviderImpl();
  ~SettingsProviderImpl() = default;

  void Bind(fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider>
                settings_provider_request);

  void AddWatcher(
      fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher);

 private:
  // |fuchsia::accessibility::SettingsProvider|
  void SetMagnificationEnabled(
      bool magnification_enabled,
      SetMagnificationEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetMagnificationZoomFactor(
      float magnification_zoom_factor,
      SetMagnificationZoomFactorCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetScreenReaderEnabled(bool screen_reader_enabled,
                              SetScreenReaderEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetColorInversionEnabled(
      bool color_inversion_enabled,
      SetColorInversionEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetColorCorrection(
      fuchsia::accessibility::ColorCorrection color_correction,
      SetColorCorrectionCallback callback) override;

  std::array<float, 9> GetColorAdjustmentMatrix();

  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::accessibility::SettingsWatcher* watcher);

  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const fuchsia::accessibility::Settings& new_settings);

  std::string BoolToString(bool value);

  fidl::Binding<fuchsia::accessibility::SettingsProvider> binding_;

  std::vector<fuchsia::accessibility::SettingsWatcherPtr> watchers_;

  fuchsia::accessibility::Settings settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingsProviderImpl);
};

}  // namespace a11y_manager
#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_PROVIDER_IMPL_H_
