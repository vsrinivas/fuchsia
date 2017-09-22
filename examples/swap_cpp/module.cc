// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/swap_cpp/module.h"

#include <utility>

namespace modular_example {

ModuleView::ModuleView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    uint32_t color)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "ModuleView"),
      background_node_(session()) {
  scenic_lib::Material background_material(session());
  background_material.SetColor((color >> 16) & 0xff, (color >> 8) & 0xff,
                               color & 0xff, (color >> 24) & 0xff);
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);
}

void ModuleView::OnPropertiesChanged(
    mozart::ViewPropertiesPtr /*old_properties*/) {
  scenic_lib::Rectangle background_shape(session(), logical_size().width,
                                             logical_size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(logical_size().width * .5f,
                                  logical_size().height * .5f, 0.f);
  InvalidateScene();
}

ModuleApp::ModuleApp(CreateViewCallback create) : create_(std::move(create)) {}

void ModuleApp::CreateView(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceRequest<app::ServiceProvider> /*services*/) {
  view_.reset(create_(
      application_context()->ConnectToEnvironmentService<mozart::ViewManager>(),
      std::move(view_owner_request)));
}

void ModuleApp::Initialize(
    fidl::InterfaceHandle<modular::ModuleContext> /*moduleContext*/,
    fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
    fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/) {}

void ModuleApp::Terminate() {
  fsl::MessageLoop::GetCurrent()->QuitNow();
}

}  // namespace modular_example
