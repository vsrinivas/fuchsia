// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_MANAGER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_MANAGER_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/macros.h>

#include "garnet/bin/a11y/a11y_manager/settings/settings_provider_impl.h"

namespace a11y_manager {
class SettingsManagerImpl : public fuchsia::accessibility::SettingsManager {
 public:
  explicit SettingsManagerImpl() = default;
  ~SettingsManagerImpl() = default;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::SettingsManager> request);

 private:
  // |fuchsia::accessibility::SettingsManager|:
  void RegisterSettingProvider(
      fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider>
          settings_provider_request) override;

  // |fuchsia::accessibility::SettingsManager|:
  void Watch(fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher>
                 watcher) override;

  fidl::BindingSet<fuchsia::accessibility::SettingsManager> bindings_;

  SettingsProviderImpl settings_provider_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingsManagerImpl);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SETTINGS_SETTINGS_MANAGER_IMPL_H_
