// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/compositor_impl.h"

#include <utility>

#include "apps/compositor/src/scene_impl.h"

namespace compositor {

CompositorImpl::CompositorImpl(CompositorEngine* engine) : engine_(engine) {}

CompositorImpl::~CompositorImpl() {}

void CompositorImpl::CreateScene(
    mojo::InterfaceRequest<mojo::gfx::composition::Scene> scene_request,
    const mojo::String& label,
    const CreateSceneCallback& callback) {
  mojo::gfx::composition::SceneTokenPtr scene_token =
      engine_->CreateScene(scene_request.Pass(), label);
  callback.Run(scene_token.Pass());
}

void CompositorImpl::CreateRenderer(
    mojo::InterfaceHandle<mojo::ContextProvider> context_provider,
    mojo::InterfaceRequest<mojo::gfx::composition::Renderer> renderer_request,
    const mojo::String& label) {
  engine_->CreateRenderer(std::move(context_provider), renderer_request.Pass(),
                          label);
}

}  // namespace compositor
