// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/services/shadertoy_factory.fidl.h"
#include "apps/mozart/examples/shadertoy/shadertoy_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

class ShadertoyApp;

// Provides a number of factory methods to create new Shadertoy instances.
class ShadertoyFactoryImpl final : public ShadertoyFactory {
 public:
  explicit ShadertoyFactoryImpl(ShadertoyApp* app);

  ~ShadertoyFactoryImpl() override;

 private:
  // |ShadertoyFactory|
  void TakeImagePipe(
      ::fidl::InterfaceRequest<Shadertoy> toy_request,
      ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe) override;

  // |ShadertoyFactory|
  void ExportMaterial(::fidl::InterfaceRequest<Shadertoy> toy_request,
                      mx::eventpair export_token) override;

  // |ShadertoyFactory|
  void CreateView(
      ::fidl::InterfaceRequest<Shadertoy> toy_request,
      ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      bool handle_input_events) override;

  ShadertoyApp* const app_;
  fidl::BindingSet<Shadertoy, std::unique_ptr<Shadertoy>> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ShadertoyFactoryImpl);
};
