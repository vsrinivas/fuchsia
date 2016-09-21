// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/renderer_impl.h"

namespace compositor {

RendererImpl::RendererImpl(
    CompositorEngine* engine,
    RendererState* state,
    mojo::InterfaceRequest<mojo::gfx::composition::Renderer> renderer_request)
    : engine_(engine),
      state_(state),
      renderer_binding_(this, renderer_request.Pass()) {}

RendererImpl::~RendererImpl() {}

void RendererImpl::GetHitTester(
    mojo::InterfaceRequest<mojo::gfx::composition::HitTester>
        hit_tester_request) {
  hit_tester_bindings.AddBinding(this, hit_tester_request.Pass());
}

void RendererImpl::SetRootScene(
    mojo::gfx::composition::SceneTokenPtr scene_token,
    uint32_t scene_version,
    mojo::RectPtr viewport) {
  engine_->SetRootScene(state_, scene_token.Pass(), scene_version,
                        viewport.Pass());
}

void RendererImpl::ClearRootScene() {
  engine_->ClearRootScene(state_);
}

void RendererImpl::GetScheduler(
    mojo::InterfaceRequest<mojo::gfx::composition::FrameScheduler>
        scheduler_request) {
  scheduler_bindings_.AddBinding(this, scheduler_request.Pass());
}

void RendererImpl::ScheduleFrame(const ScheduleFrameCallback& callback) {
  engine_->ScheduleFrame(state_,
                         [callback](mojo::gfx::composition::FrameInfoPtr info) {
                           callback.Run(std::move(info));
                         });
}

void RendererImpl::HitTest(mojo::PointFPtr point,
                           const HitTestCallback& callback) {
  engine_->HitTest(state_, point.Pass(), callback);
}

}  // namespace compositor
