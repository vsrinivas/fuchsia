// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/settings_manager.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/syscalls.h>

#include <string>

#include "lib/fidl/cpp/clone.h"

// DEPRECATED

namespace {

std::string BoolToString(bool value) { return value ? "true" : "false"; }

}  // namespace

namespace a11y_manager {

SettingsManagerImpl::SettingsManagerImpl() : settings_() {
  settings_.set_magnification_enabled(false);
  settings_.set_magnification_zoom_factor(1.0);
  settings_.set_screen_reader_enabled(false);
  settings_.set_color_inversion_enabled(false);
  settings_.set_color_correction(
      fuchsia::accessibility::ColorCorrection::DISABLED);
}

void SettingsManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SettingsManagerImpl::GetSettings(GetSettingsCallback callback) {
  FX_LOGS(INFO) << "GetSettings()";

  callback(fuchsia::accessibility::SettingsManagerStatus::OK,
           fidl::Clone(settings_));
}

void SettingsManagerImpl::SetMagnificationEnabled(
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

  // TODO: Write settings to file (or other output location).

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsManagerImpl::SetMagnificationZoomFactor(
    float_t magnification_zoom_factor,
    SetMagnificationZoomFactorCallback callback) {
  if (!settings_.has_magnification_enabled() ||
      !(settings_.magnification_enabled())) {
    FX_LOGS(ERROR) << "Magnification must be enabled to set zoom factor.";

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

  // TODO: Write settings to file (or other output location).

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsManagerImpl::SetScreenReaderEnabled(
    bool screen_reader_enabled, SetScreenReaderEnabledCallback callback) {
  settings_.set_screen_reader_enabled(screen_reader_enabled);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "screen_reader_enabled = "
                << BoolToString(settings_.screen_reader_enabled());

  // TODO: Write settings to file (or other output location).

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsManagerImpl::SetColorInversionEnabled(
    bool color_inversion_enabled, SetColorInversionEnabledCallback callback) {
  settings_.set_color_inversion_enabled(color_inversion_enabled);

  NotifyWatchers(settings_);

  FX_LOGS(INFO) << "color_inversion_enabled = "
                << BoolToString(settings_.color_inversion_enabled());

  // TODO: Write settings to file (or other output location).

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsManagerImpl::SetColorCorrection(
    fuchsia::accessibility::ColorCorrection color_correction,
    SetColorCorrectionCallback callback) {
  settings_.set_color_correction(color_correction);

  NotifyWatchers(settings_);

  // TODO: Write settings to file (or other output location).

  callback(fuchsia::accessibility::SettingsManagerStatus::OK);
}

void SettingsManagerImpl::NotifyWatchers(
    const fuchsia::accessibility::Settings& new_settings) {
  for (auto& watcher : watchers_) {
    watcher->OnSettingsChange(fidl::Clone(new_settings));
  }
}

void SettingsManagerImpl::ReleaseWatcher(
    fuchsia::accessibility::SettingsWatcher* watcher) {
  auto predicate = [watcher](const auto& target) {
    return target.get() == watcher;
  };

  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void SettingsManagerImpl::Watch(
    fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) {
  fuchsia::accessibility::SettingsWatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::accessibility::SettingsWatcher* proxy_raw_ptr = watcher_proxy.get();

  watcher_proxy.set_error_handler([this, proxy_raw_ptr](zx_status_t status) {
    ReleaseWatcher(proxy_raw_ptr);
  });
  watchers_.push_back(std::move(watcher_proxy));
}

}  // namespace a11y_manager
