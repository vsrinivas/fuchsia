// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "garnet/bin/a11y/a11y_manager/manager_impl.h"
#include "garnet/bin/a11y/a11y_manager/semantics/semantics_manager_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace a11y_manager {

// A11y manager application entry point.
class App {
 public:
  App();
  ~App() = default;

 private:
  std::unique_ptr<sys::ComponentContext> startup_context_;

  std::unique_ptr<ManagerImpl> a11y_manager_;
  // TODO(MI4-1567): Re-add Settings Manager when re-design is complete.
  // std::unique_ptr<SettingsManagerImpl> settings_manager_impl_;
  std::unique_ptr<SemanticsManagerImpl> semantics_manager_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_
