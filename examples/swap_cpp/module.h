// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_
#define APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/story/module.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "third_party/skia/include/core/SkColor.h"

namespace modular_example {

class ModuleView : public mozart::BaseView {
 public:
  explicit ModuleView(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      SkColor color);

 private:
  // |BaseView|:
  void OnDraw() override;

  SkColor color_;
  mozart::BufferProducer buffer_producer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleView);
};

class ModuleApp : public modular::SingleServiceViewApp<modular::Module> {
 public:
  using CreateViewCallback = std::function<mozart::BaseView*(
      mozart::ViewManagerPtr,
      fidl::InterfaceRequest<mozart::ViewOwner>)>;

  explicit ModuleApp(CreateViewCallback create);

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::Story> story,
      fidl::InterfaceHandle<modular::Link> link,
      fidl::InterfaceHandle<modular::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<modular::ServiceProvider> outgoing_services)
      override;

  // |Module|
  void Stop(const StopCallback& done) override;

  CreateViewCallback create_;
  std::unique_ptr<mozart::BaseView> view_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModuleApp);
};

}  // namespace modula_example

#endif  // APPS_MODULAR_EXAMPLES_SWAP_CPP_MODULE_H_
