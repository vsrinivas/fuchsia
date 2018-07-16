// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_
#define PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/ui/view_framework/base_view.h>

#include "peridot/lib/fidl/single_service_app.h"

namespace modular_example {

class ModuleView : public mozart::BaseView {
 public:
  explicit ModuleView(
      fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      uint32_t color);

 private:
  // |BaseView|:
  void OnPropertiesChanged(
      fuchsia::ui::views_v1::ViewProperties old_properties) override;

  scenic::ShapeNode background_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleView);
};

class ModuleApp : public modular::ViewApp {
 public:
  using CreateViewCallback = std::function<mozart::BaseView*(
      fuchsia::ui::views_v1::ViewManagerPtr,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>)>;

  explicit ModuleApp(component::StartupContext* const startup_context,
                     CreateViewCallback create);

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) override;

  CreateViewCallback create_;
  std::unique_ptr<mozart::BaseView> view_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleApp);
};

}  // namespace modular_example

#endif  // PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_
