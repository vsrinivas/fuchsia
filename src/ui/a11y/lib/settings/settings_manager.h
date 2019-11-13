// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_MANAGER_H_

#include <fuchsia/accessibility/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/settings/settings_provider.h"

namespace a11y {
class SettingsManager : public fuchsia::accessibility::SettingsManager {
 public:
  SettingsManager();
  ~SettingsManager() override;

  // |fuchsia::accessibility::SettingsManager|:
  void RegisterSettingProvider(fidl::InterfaceRequest<fuchsia::accessibility::SettingsProvider>
                                   settings_provider_request) override;

  // |fuchsia::accessibility::SettingsManager|:
  void Watch(fidl::InterfaceHandle<fuchsia::accessibility::SettingsWatcher> watcher) override;

  // Returns a copy of current set of settings.
  fuchsia::accessibility::SettingsPtr GetSettings() const;

 private:
  SettingsProvider settings_provider_;
  fidl::Binding<fuchsia::accessibility::SettingsProvider> settings_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingsManager);
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SETTINGS_SETTINGS_MANAGER_H_
