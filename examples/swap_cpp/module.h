// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_
#define APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/services/module/module.fidl.h"
#include "lib/ui/view_framework/base_view.h"

namespace modular_example {

class ModuleView : public mozart::BaseView {
 public:
  explicit ModuleView(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      uint32_t color);

 private:
  // |BaseView|:
  void OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) override;

  scenic_lib::ShapeNode background_node_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleView);
};

class ModuleApp : public modular::SingleServiceApp<modular::Module> {
 public:
  using CreateViewCallback = std::function<mozart::BaseView*(
      mozart::ViewManagerPtr,
      fidl::InterfaceRequest<mozart::ViewOwner>)>;

  explicit ModuleApp(CreateViewCallback create);

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> moduleContext,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override;

  // |Lifecycle|
  void Terminate() override;

  CreateViewCallback create_;
  std::unique_ptr<mozart::BaseView> view_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleApp);
};

}  // namespace modular_example

#endif  // APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_
