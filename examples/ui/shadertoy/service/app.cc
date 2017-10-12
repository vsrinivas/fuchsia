// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/app.h"

#include "escher/vk/vulkan_device_queues.h"
#include "escher/vk/vulkan_instance.h"
#include "lib/app/cpp/application_context.h"

namespace shadertoy {

App::App(app::ApplicationContext* app_context, escher::Escher* escher)
    : escher_(escher),
      renderer_(escher, kDefaultImageFormat),
      compiler_(escher,
                renderer_.render_pass(),
                renderer_.descriptor_set_layout()) {
  app_context->outgoing_services()
      ->AddService<mozart::example::ShadertoyFactory>(
          [this](fidl::InterfaceRequest<mozart::example::ShadertoyFactory>
                     request) {
            FXL_LOG(INFO) << "Accepting connection to ShadertoyFactory";
            factory_bindings_.AddBinding(this, std::move(request));
          });
}

App::~App() = default;

void App::NewImagePipeShadertoy(
    ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
    ::fidl::InterfaceHandle<scenic::ImagePipe> image_pipe) {
  shadertoy_bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(
          ShadertoyState::NewForImagePipe(this, std::move(image_pipe))),
      std::move(toy_request));
}

void App::NewViewShadertoy(
    ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
    ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    bool handle_input_events) {
  shadertoy_bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(ShadertoyState::NewForView(
          this, std::move(view_owner_request), handle_input_events)),
      std::move(toy_request));
}

void App::CloseShadertoy(ShadertoyState* shadertoy) {
  auto end = shadertoy_bindings_.end();
  for (auto it = shadertoy_bindings_.begin(); it != end; ++it) {
    auto impl = static_cast<ShadertoyImpl*>((*it)->impl());
    if (shadertoy == impl->state()) {
      (*it)->Close();
      return;
    }
  }
}

}  // namespace shadertoy
