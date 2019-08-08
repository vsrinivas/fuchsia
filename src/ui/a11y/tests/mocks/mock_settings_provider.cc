// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/tests/mocks/mock_settings_provider.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace accessibility_test {

MockSettingsProvider::MockSettingsProvider(sys::testing::ComponentContextProvider* context)
    : context_(context) {
  context_->ConnectToPublicService(manager_.NewRequest());
  manager_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to SettingsManager with status:"
                   << zx_status_get_string(status);
  });

  manager_->RegisterSettingProvider(settings_provider_ptr_.NewRequest());
}

void MockSettingsProvider::SetMagnificationEnabled(
    bool magnification_enabled, SettingsProvider::SetMagnificationEnabledCallback callback) {
  settings_provider_ptr_->SetMagnificationEnabled(magnification_enabled, std::move(callback));
}
void MockSettingsProvider::SetMagnificationZoomFactor(
    float magnification_zoom_factor,
    SettingsProvider::SetMagnificationZoomFactorCallback callback) {
  settings_provider_ptr_->SetMagnificationZoomFactor(magnification_zoom_factor,
                                                     std::move(callback));
}

void MockSettingsProvider::SetScreenReaderEnabled(
    bool screen_reader_enabled, SettingsProvider::SetScreenReaderEnabledCallback callback) {
  settings_provider_ptr_->SetScreenReaderEnabled(screen_reader_enabled, std::move(callback));
}

void MockSettingsProvider::SetColorInversionEnabled(
    bool color_inversion_enabled, SettingsProvider::SetColorInversionEnabledCallback callback) {
  settings_provider_ptr_->SetColorInversionEnabled(color_inversion_enabled, std::move(callback));
}

void MockSettingsProvider::SetColorCorrection(
    ColorCorrection color_correction, SettingsProvider::SetColorCorrectionCallback callback) {
  settings_provider_ptr_->SetColorCorrection(color_correction, std::move(callback));
}

}  // namespace accessibility_test
