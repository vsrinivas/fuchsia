// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/swap_cpp/module.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <utility>

namespace modular_example {

ModuleView::ModuleView(scenic::ViewContext view_context, uint32_t color)
    : BaseView(std::move(view_context), "ModuleView"),
      background_node_(session()) {
  scenic::Material background_material(session());
  background_material.SetColor((color >> 16) & 0xff, (color >> 8) & 0xff,
                               color & 0xff, (color >> 24) & 0xff);
  background_node_.SetMaterial(background_material);
  root_node().AddChild(background_node_);
}

void ModuleView::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties) {
  scenic::Rectangle background_shape(session(), logical_size().x,
                                     logical_size().y);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(logical_size().x * .5f,
                                  logical_size().y * .5f, 0.f);
}

ModuleApp::ModuleApp(component::StartupContext* const startup_context,
                     CreateViewCallback create)
    : ViewApp(startup_context), create_(std::move(create)) {}

void ModuleApp::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  auto scenic =
      startup_context()
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic::ViewContext context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
      .view_token2 = scenic::ToViewToken(std::move(view_token)),
      .incoming_services = std::move(incoming_services),
      .outgoing_services = std::move(outgoing_services),
      .startup_context = startup_context(),
  };

  view_.reset(create_(std::move(context)));
}

}  // namespace modular_example
