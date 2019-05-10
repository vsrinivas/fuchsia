// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/settings/settings_manager_impl.h"

#include <lib/syslog/cpp/logger.h>
#include <src/lib/fxl/logging.h>

namespace a11y_manager {

void SettingsManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SettingsManagerImpl::RegisterSettingProvider(
    fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider>
        settings_provider_request) {
  settings_provider_impl_.Bind(std::move(settings_provider_request));
}

void SettingsManagerImpl::Watch(
    fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) {
  settings_provider_impl_.AddWatcher(std::move(watcher));
}

}  // namespace a11y_manager
