// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/settings/settings_manager.h"

#include <lib/syslog/cpp/logger.h>

#include <src/lib/fxl/logging.h>

namespace a11y {

SettingsManager::SettingsManager() = default;
SettingsManager::~SettingsManager() = default;

void SettingsManager::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SettingsManager::RegisterSettingProvider(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider> settings_provider_request) {
  settings_provider_.Bind(std::move(settings_provider_request));
}

void SettingsManager::Watch(
    fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) {
  settings_provider_.AddWatcher(std::move(watcher));
}

}  // namespace a11y
