// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/compositor_impl.h"

#include <utility>

#include "apps/mozart/src/compositor/scene_impl.h"

namespace compositor {

CompositorImpl::CompositorImpl(CompositorEngine* engine) : engine_(engine) {}

CompositorImpl::~CompositorImpl() {}

void CompositorImpl::CreateScene(
    mojo::InterfaceRequest<mozart::Scene> scene_request,
    const mojo::String& label,
    const CreateSceneCallback& callback) {
  mozart::SceneTokenPtr scene_token =
      engine_->CreateScene(scene_request.Pass(), label);
  callback.Run(scene_token.Pass());
}

void CompositorImpl::CreateRenderer(
    mojo::InterfaceRequest<mozart::Renderer> renderer_request,
    const mojo::String& label) {
  engine_->CreateRenderer(renderer_request.Pass(), label);
}

}  // namespace compositor
