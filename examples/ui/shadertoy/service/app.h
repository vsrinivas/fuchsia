// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/ui/shadertoy/service/services/shadertoy_factory.fidl.h"
#include "garnet/examples/ui/shadertoy/service/shadertoy_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

#include "garnet/examples/ui/shadertoy/service/compiler.h"
#include "garnet/examples/ui/shadertoy/service/renderer.h"

namespace shadertoy {

class ShadertoyState;

// A thin wrapper that manages connections to a ShadertoyFactoryImpl singleton.
// TODO: clean up when there are no remaining bindings to Shadertoy nor
// ShadertoyFactory.  What is the best-practice pattern to use here?
class App : public mozart::example::ShadertoyFactory {
 public:
  App(app::ApplicationContext* app_context, escher::Escher* escher);
  ~App();

  escher::Escher* escher() const { return escher_; }
  Compiler* compiler() { return &compiler_; }
  Renderer* renderer() { return &renderer_; }

  static constexpr vk::Format kDefaultImageFormat = vk::Format::eB8G8R8A8Srgb;

 private:
  friend class ShadertoyState;

  // Called by ShadertoyState::Close().
  void CloseShadertoy(ShadertoyState* shadertoy);

  // |ShadertoyFactory|
  void NewImagePipeShadertoy(
      ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
      ::fidl::InterfaceHandle<scenic::ImagePipe> image_pipe) override;

  // |ShadertoyFactory|
  void NewViewShadertoy(
      ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
      ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      bool handle_input_events) override;

  fidl::BindingSet<mozart::example::ShadertoyFactory> factory_bindings_;
  fidl::BindingSet<mozart::example::Shadertoy,
                   std::unique_ptr<mozart::example::Shadertoy>>
      shadertoy_bindings_;

  escher::Escher* const escher_;
  Renderer renderer_;
  Compiler compiler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace shadertoy
