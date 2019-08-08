// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_TESTS_MOCKS_MOCK_SETTINGS_PROVIDER_H_
#define SRC_UI_A11Y_TESTS_MOCKS_MOCK_SETTINGS_PROVIDER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <math.h>

#include <src/lib/fxl/macros.h>

namespace accessibility_test {
using fuchsia::accessibility::ColorCorrection;
using fuchsia::accessibility::Settings;
using fuchsia::accessibility::SettingsManagerPtr;
using fuchsia::accessibility::SettingsProvider;
using fuchsia::accessibility::SettingsProviderPtr;

// This is a mock of Fuchsia Settings Service, which provides all the
// Accessibility settings, and used solely for testing Accessiblity Settings.
class MockSettingsProvider {
 public:
  explicit MockSettingsProvider(sys::testing::ComponentContextProvider* context);
  ~MockSettingsProvider() = default;

  void SetMagnificationEnabled(bool magnification_enabled,
                               SettingsProvider::SetMagnificationEnabledCallback callback);

  void SetMagnificationZoomFactor(float magnification_zoom_factor,
                                  SettingsProvider::SetMagnificationZoomFactorCallback callback);

  void SetScreenReaderEnabled(bool screen_reader_enabled,
                              SettingsProvider::SetScreenReaderEnabledCallback callback);

  void SetColorInversionEnabled(bool color_inversion_enabled,
                                SettingsProvider::SetColorInversionEnabledCallback callback);

  void SetColorCorrection(ColorCorrection color_correction,
                          SettingsProvider::SetColorCorrectionCallback callback);

 private:
  sys::testing::ComponentContextProvider* context_;
  SettingsManagerPtr manager_;
  SettingsProviderPtr settings_provider_ptr_;
  Settings settings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSettingsProvider);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_TESTS_MOCKS_MOCK_SETTINGS_PROVIDER_H_
