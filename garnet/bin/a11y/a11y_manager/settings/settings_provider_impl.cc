// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/settings/settings_provider_impl.h"

#include <lib/syslog/cpp/logger.h>
#include <src/lib/fxl/logging.h>

namespace a11y_manager {
const std::array<float, 9> kIdentityMatrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};

SettingsProviderImpl::SettingsProviderImpl() : binding_(this), settings_() {
  settings_.set_magnification_enabled(false);
  settings_.set_magnification_zoom_factor(1.0);
  settings_.set_screen_reader_enabled(false);
  settings_.set_color_inversion_enabled(false);
  settings_.set_color_correction(
      fuchsia::accessibility::ColorCorrection::DISABLED);
  settings_.set_color_adjustment_matrix(kIdentityMatrix);
}

void SettingsProviderImpl::Bind(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider>
        settings_provider_request) {
  binding_.Close(ZX_ERR_PEER_CLOSED);
  binding_.Bind(std::move(settings_provider_request));
}

std::string SettingsProviderImpl::BoolToString(bool value) {
  return value ? "true" : "false";
}

void SettingsProviderImpl::SetMagnificationEnabled(
    bool magnification_enabled, SetMagnificationEnabledCallback callback) {
  // Attempting to enable magnification when it's already enabled OR disable
  // magnification when it's already disabled has no effect.
  if (settings_.has_magnification_enabled() &&
      settings_.magnification_enabled() == magnification_enabled) {
    callback(fuchsia::accessibility::SettingsManagerStatus::OK);
    return;
  }

  if (magnification_enabled) {
    // Case: enable magnification (currently disabled).
    settings_.set_magnification_enabled(true);
  } else {
    // Case: disable magnification (currently enabled).
    settings_.set_magnification_enabled(false);
  }

  // In either case, set zoom factor to default value of 1.0.
  settings_.set_magnification_zoom_factor(1.0);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "magnification_enabled = "
                << BoolToString(settings_.magnification_enabled());

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsProviderImpl::SetMagnificationZoomFactor(
    float magnification_zoom_factor,
    SetMagnificationZoomFactorCallback callback) {
  if (!settings_.has_magnification_enabled() ||
      !(settings_.magnification_enabled())) {
    callback(fuchsia::accessibility::SettingsManagerStatus::ERROR);
    return;
  }

  if (magnification_zoom_factor < 1.0) {
    FX_LOGS(ERROR) << "Magnification zoom factor must be > 1.0.";

    callback(fuchsia::accessibility::SettingsManagerStatus::ERROR);
    return;
  }

  settings_.set_magnification_zoom_factor(magnification_zoom_factor);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "magnification_zoom_factor = "
                << settings_.magnification_zoom_factor();

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsProviderImpl::SetScreenReaderEnabled(
    bool screen_reader_enabled, SetScreenReaderEnabledCallback callback) {
  settings_.set_screen_reader_enabled(screen_reader_enabled);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "screen_reader_enabled = "
                << BoolToString(settings_.screen_reader_enabled());

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsProviderImpl::SetColorInversionEnabled(
    bool color_inversion_enabled, SetColorInversionEnabledCallback callback) {
  settings_.set_color_inversion_enabled(color_inversion_enabled);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "color_inversion_enabled = "
                << BoolToString(settings_.color_inversion_enabled());

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsProviderImpl::SetColorCorrection(
    fuchsia::accessibility::ColorCorrection color_correction,
    SetColorCorrectionCallback callback) {
  settings_.set_color_correction(color_correction);

  NotifyWatchers(settings_);

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsProviderImpl::NotifyWatchers(
    const fuchsia::accessibility::Settings& new_settings) {
  for (auto& watcher : watchers_) {
    auto setting = fidl::Clone(new_settings);
    watcher->OnSettingsChange(std::move(setting));
  }
}

void SettingsProviderImpl::ReleaseWatcher(
    fuchsia::accessibility::SettingsWatcher* watcher) {
  auto predicate = [watcher](const auto& target) {
    return target.get() == watcher;
  };

  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void SettingsProviderImpl::AddWatcher(
    fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) {
  fuchsia::accessibility::SettingsWatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::accessibility::SettingsWatcher* proxy_raw_ptr = watcher_proxy.get();

  watcher_proxy.set_error_handler([this, proxy_raw_ptr](zx_status_t status) {
    ReleaseWatcher(proxy_raw_ptr);
  });
  // Send Current Settings to watcher, so that they have the initial copy of the
  // Settings.
  auto setting = fidl::Clone(settings_);
  watcher_proxy->OnSettingsChange(std::move(setting));
  watchers_.push_back(std::move(watcher_proxy));
}

}  // namespace a11y_manager
