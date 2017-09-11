// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "garnet/examples/ui/shadertoy/service/shadertoy_factory_impl.h"
#include "escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

#include "garnet/examples/ui/shadertoy/service/compiler.h"
#include "garnet/examples/ui/shadertoy/service/renderer.h"

namespace shadertoy {

// A thin wrapper that manages connections to a ShadertoyFactoryImpl singleton.
// TODO: clean up when there are no remaining bindings to Shadertoy nor
// ShadertoyFactory.  What is the best-practice pattern to use here?
class App {
 public:
  App(app::ApplicationContext* app_context, escher::Escher* escher);
  ~App();

  escher::Escher* escher() const { return escher_; }
  Compiler* compiler() { return &compiler_; }
  Renderer* renderer() { return &renderer_; }

  static constexpr vk::Format kDefaultImageFormat = vk::Format::eB8G8R8A8Srgb;

 private:
  std::unique_ptr<ShadertoyFactoryImpl> factory_;
  fidl::BindingSet<mozart::example::ShadertoyFactory> bindings_;
  escher::Escher* const escher_;
  Renderer renderer_;
  Compiler compiler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace shadertoy
