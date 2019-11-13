// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_PROVIDER_H_
#define SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_PROVIDER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <math.h>

#include <array>

#include "src/lib/fxl/macros.h"

namespace a11y {

class SettingsProvider : public fuchsia::accessibility::SettingsProvider {
 public:
  SettingsProvider();
  ~SettingsProvider() override;

  void AddWatcher(fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher);

  fuchsia::accessibility::SettingsPtr GetSettings() const;

 private:
  // |fuchsia::accessibility::SettingsProvider|
  void SetMagnificationEnabled(bool magnification_enabled,
                               SetMagnificationEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetMagnificationZoomFactor(float magnification_zoom_factor,
                                  SetMagnificationZoomFactorCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetScreenReaderEnabled(bool screen_reader_enabled,
                              SetScreenReaderEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetColorInversionEnabled(bool color_inversion_enabled,
                                SetColorInversionEnabledCallback callback) override;

  // |fuchsia::accessibility::SettingsProvider|
  void SetColorCorrection(fuchsia::accessibility::ColorCorrection color_correction,
                          SetColorCorrectionCallback callback) override;

  std::array<float, 9> GetColorAdjustmentMatrix();

  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const fuchsia::accessibility::Settings& new_settings);

  fidl::InterfacePtrSet<fuchsia::accessibility::SettingsWatcher> watchers_;

  fuchsia::accessibility::Settings settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingsProvider);
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_PROVIDER_H_
