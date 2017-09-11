// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/ui/shadertoy/service/services/shadertoy_factory.fidl.h"
#include "garnet/examples/ui/shadertoy/service/shadertoy_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace shadertoy {

class App;

// Provides a number of factory methods to create new Shadertoy instances.
class ShadertoyFactoryImpl final : public mozart::example::ShadertoyFactory {
 public:
  explicit ShadertoyFactoryImpl(App* app);

  ~ShadertoyFactoryImpl() override;

 private:
  // |ShadertoyFactory|
  void NewImagePipeShadertoy(
      ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
      ::fidl::InterfaceHandle<scenic::ImagePipe> image_pipe) override;

  // |ShadertoyFactory|
  void NewViewShadertoy(
      ::fidl::InterfaceRequest<mozart::example::Shadertoy> toy_request,
      ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      bool handle_input_events) override;

  App* const app_;
  fidl::BindingSet<mozart::example::Shadertoy,
                   std::unique_ptr<mozart::example::Shadertoy>>
      bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShadertoyFactoryImpl);
};

}  // namespace shadertoy
