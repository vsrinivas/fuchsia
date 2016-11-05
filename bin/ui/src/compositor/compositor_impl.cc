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
    fidl::InterfaceRequest<mozart::Scene> scene_request,
    const fidl::String& label,
    const CreateSceneCallback& callback) {
  mozart::SceneTokenPtr scene_token =
      engine_->CreateScene(std::move(scene_request), label);
  callback(std::move(scene_token));
}

void CompositorImpl::CreateRenderer(
    fidl::InterfaceRequest<mozart::Renderer> renderer_request,
    const fidl::String& label) {
  engine_->CreateRenderer(std::move(renderer_request), label);
}

}  // namespace compositor
