// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/shadertoy_factory_impl.h"
#include "apps/mozart/examples/shadertoy/shadertoy_state.h"

ShadertoyFactoryImpl::ShadertoyFactoryImpl(ShadertoyApp* app) : app_(app) {}

ShadertoyFactoryImpl::~ShadertoyFactoryImpl() = default;

void ShadertoyFactoryImpl::TakeImagePipe(
    ::fidl::InterfaceRequest<Shadertoy> toy_request,
    ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe) {
  bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(
          ShadertoyState::NewForImagePipe(app_, std::move(image_pipe))),
      std::move(toy_request));
}

void ShadertoyFactoryImpl::ExportMaterial(
    ::fidl::InterfaceRequest<Shadertoy> toy_request,
    mx::eventpair export_token) {
  bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(
          ShadertoyState::NewForMaterial(app_, std::move(export_token))),
      std::move(toy_request));
}

void ShadertoyFactoryImpl::CreateView(
    ::fidl::InterfaceRequest<Shadertoy> toy_request,
    ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    bool handle_input_events) {
  bindings_.AddBinding(
      std::make_unique<ShadertoyImpl>(ShadertoyState::NewForView(
          app_, std::move(view_owner_request), handle_input_events)),
      std::move(toy_request));
}
