// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_

#include <memory>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewManagerImpl;

// View manager application entry point.
class ViewManagerApp {
 public:
  explicit ViewManagerApp();
  ~ViewManagerApp();

 private:
  std::unique_ptr<component::StartupContext> startup_context_;

  std::unique_ptr<ViewRegistry> registry_;
  fidl::BindingSet<::fuchsia::ui::viewsv1::ViewManager,
                   std::unique_ptr<ViewManagerImpl>>
      view_manager_bindings_;

  // Binding to expose view hit-test service to the a11y manager.
  fidl::BindingSet<fuchsia::ui::viewsv1::AccessibilityViewInspector>
      inspector_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewManagerApp);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_APP_H_
