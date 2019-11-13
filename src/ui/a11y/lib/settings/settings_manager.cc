// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/settings/settings_manager.h"

namespace a11y {

SettingsManager::SettingsManager() : settings_provider_binding_(&settings_provider_){};
SettingsManager::~SettingsManager() = default;

void SettingsManager::RegisterSettingProvider(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider> settings_provider_request) {
  settings_provider_binding_.Bind(std::move(settings_provider_request));
}

void SettingsManager::Watch(
    fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) {
  settings_provider_.AddWatcher(std::move(watcher));
}

fuchsia::accessibility::SettingsPtr SettingsManager::GetSettings() const {
  return settings_provider_.GetSettings();
}

}  // namespace a11y
