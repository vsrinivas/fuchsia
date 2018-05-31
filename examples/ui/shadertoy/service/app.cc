// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/app.h"

#include "lib/app/cpp/application_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/escher/vk/vulkan_instance.h"

namespace shadertoy {

App::App(async::Loop* loop, component::ApplicationContext* app_context,
         escher::Escher* escher)
    : escher_(escher),
      renderer_(escher, kDefaultImageFormat),
      compiler_(loop, escher, renderer_.render_pass(),
                renderer_.descriptor_set_layout()) {
  app_context->outgoing().AddPublicService<::fuchsia::ui::shadertoy::ShadertoyFactory>(
      [this](fidl::InterfaceRequest<::fuchsia::ui::shadertoy::ShadertoyFactory> request) {
        FXL_LOG(INFO) << "Accepting connection to ShadertoyFactory";
        factory_bindings_.AddBinding(this, std::move(request));
      });
}

App::~App() = default;

void App::NewImagePipeShadertoy(
    ::fidl::InterfaceRequest<::fuchsia::ui::shadertoy::Shadertoy> toy_request,
    ::fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe) {
  shadertoy_bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(
          ShadertoyState::NewForImagePipe(this, std::move(image_pipe))),
      std::move(toy_request));
}

void App::NewViewShadertoy(
    ::fidl::InterfaceRequest<::fuchsia::ui::shadertoy::Shadertoy> toy_request,
    ::fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
    bool handle_input_events) {
  shadertoy_bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(ShadertoyState::NewForView(
          this, std::move(view_owner_request), handle_input_events)),
      std::move(toy_request));
}

void App::CloseShadertoy(ShadertoyState* shadertoy) {
  for (auto& binding : shadertoy_bindings_.bindings()) {
    if (binding && shadertoy == binding->impl()->state()) {
      binding->Unbind();
      return;
    }
  }
}

}  // namespace shadertoy
