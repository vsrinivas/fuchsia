// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/mocks/mock_settings_service.h"

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace accessibility_test {

MockSettingsService::MockSettingsService(
    sys::testing::ComponentContextProvider* context)
    : context_(context) {
  context_->context()->svc()->Connect(manager_.NewRequest());
  manager_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to SettingsManager with status:"
                   << zx_status_get_string(status);
  });

  manager_->RegisterSettingProvider(settings_provider_ptr_.NewRequest());
}

void MockSettingsService::SetMagnificationEnabled(
    bool magnification_enabled,
    fuchsia::accessibility::SettingsProvider::SetMagnificationEnabledCallback
        callback) {
  settings_provider_ptr_->SetMagnificationEnabled(magnification_enabled,
                                                  std::move(callback));
}
void MockSettingsService::SetMagnificationZoomFactor(
    float magnification_zoom_factor,
    fuchsia::accessibility::SettingsProvider::SetMagnificationZoomFactorCallback
        callback) {
  settings_provider_ptr_->SetMagnificationZoomFactor(magnification_zoom_factor,
                                                     std::move(callback));
}

void MockSettingsService::SetScreenReaderEnabled(
    bool screen_reader_enabled,
    fuchsia::accessibility::SettingsProvider::SetScreenReaderEnabledCallback
        callback) {
  settings_provider_ptr_->SetScreenReaderEnabled(screen_reader_enabled,
                                                 std::move(callback));
}

void MockSettingsService::SetColorInversionEnabled(
    bool color_inversion_enabled,
    fuchsia::accessibility::SettingsProvider::SetColorInversionEnabledCallback
        callback) {
  settings_provider_ptr_->SetColorInversionEnabled(color_inversion_enabled,
                                                   std::move(callback));
}

void MockSettingsService::SetColorCorrection(
    fuchsia::accessibility::ColorCorrection color_correction,
    fuchsia::accessibility::SettingsProvider::SetColorCorrectionCallback
        callback) {
  settings_provider_ptr_->SetColorCorrection(color_correction,
                                             std::move(callback));
}

}  // namespace accessibility_test
