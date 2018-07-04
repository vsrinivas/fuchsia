// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_APP_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_APP_H_

#include <fuchsia/examples/shadertoy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/examples/ui/shadertoy/service/shadertoy_impl.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/escher/escher.h"
#include "lib/fidl/cpp/binding_set.h"

#include "garnet/examples/ui/shadertoy/service/compiler.h"
#include "garnet/examples/ui/shadertoy/service/renderer.h"

namespace shadertoy {

class ShadertoyState;

// A thin wrapper that manages connections to a ShadertoyFactoryImpl singleton.
// TODO: clean up when there are no remaining bindings to Shadertoy nor
// ShadertoyFactory.  What is the best-practice pattern to use here?
class App : public fuchsia::examples::shadertoy::ShadertoyFactory {
 public:
  App(async::Loop* loop, fuchsia::sys::StartupContext* app_context,
      escher::EscherWeakPtr escher);
  ~App();

  escher::Escher* escher() const { return escher_.get(); }

  Compiler* compiler() { return &compiler_; }
  Renderer* renderer() { return &renderer_; }

  static constexpr vk::Format kDefaultImageFormat = vk::Format::eB8G8R8A8Srgb;

 private:
  friend class ShadertoyState;

  // Called by ShadertoyState::Close().
  void CloseShadertoy(ShadertoyState* shadertoy);

  // |ShadertoyFactory|
  void NewImagePipeShadertoy(
      ::fidl::InterfaceRequest<fuchsia::examples::shadertoy::Shadertoy>
          toy_request,
      ::fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe) override;

  // |ShadertoyFactory|
  void NewViewShadertoy(
      ::fidl::InterfaceRequest<fuchsia::examples::shadertoy::Shadertoy>
          toy_request,
      ::fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      bool handle_input_events) override;

  fidl::BindingSet<fuchsia::examples::shadertoy::ShadertoyFactory>
      factory_bindings_;
  fidl::BindingSet<fuchsia::examples::shadertoy::Shadertoy,
                   std::unique_ptr<ShadertoyImpl>>
      shadertoy_bindings_;

  const escher::EscherWeakPtr escher_;
  Renderer renderer_;
  Compiler compiler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_APP_H_
