// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/tests/fakes/fake_settings_service.h"

#include <zircon/status.h>

#include "src/lib/syslog/cpp/logger.h"

namespace root_presenter {
namespace testing {

FakeSettingsService::FakeSettingsService(sys::ComponentContext& context) {
  context.svc()->Connect(manager_.NewRequest());
  manager_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Cannot connect to SettingsManager with status:"
                   << zx_status_get_string(status);
  });

  manager_->RegisterSettingProvider(settings_provider_ptr_.NewRequest());
}

void FakeSettingsService::SetMagnificationEnabled(
    bool magnification_enabled,
    fuchsia::accessibility::SettingsProvider::SetMagnificationEnabledCallback callback) {
  settings_provider_ptr_->SetMagnificationEnabled(magnification_enabled, std::move(callback));
}
void FakeSettingsService::SetMagnificationZoomFactor(
    float magnification_zoom_factor,
    fuchsia::accessibility::SettingsProvider::SetMagnificationZoomFactorCallback callback) {
  settings_provider_ptr_->SetMagnificationZoomFactor(magnification_zoom_factor,
                                                     std::move(callback));
}

void FakeSettingsService::SetScreenReaderEnabled(
    bool screen_reader_enabled,
    fuchsia::accessibility::SettingsProvider::SetScreenReaderEnabledCallback callback) {
  settings_provider_ptr_->SetScreenReaderEnabled(screen_reader_enabled, std::move(callback));
}

void FakeSettingsService::SetColorInversionEnabled(
    bool color_inversion_enabled,
    fuchsia::accessibility::SettingsProvider::SetColorInversionEnabledCallback callback) {
  settings_provider_ptr_->SetColorInversionEnabled(color_inversion_enabled, std::move(callback));
}

void FakeSettingsService::SetColorCorrection(
    fuchsia::accessibility::ColorCorrection color_correction,
    fuchsia::accessibility::SettingsProvider::SetColorCorrectionCallback callback) {
  settings_provider_ptr_->SetColorCorrection(color_correction, std::move(callback));
}

}  // namespace testing
}  // namespace root_presenter
