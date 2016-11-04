// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/renderer_impl.h"

namespace compositor {

RendererImpl::RendererImpl(
    CompositorEngine* engine,
    RendererState* state,
    mojo::InterfaceRequest<mozart::Renderer> renderer_request)
    : engine_(engine),
      state_(state),
      renderer_binding_(this, renderer_request.Pass()) {}

RendererImpl::~RendererImpl() {}

void RendererImpl::GetHitTester(
    mojo::InterfaceRequest<mozart::HitTester> hit_tester_request) {
  hit_tester_bindings.AddBinding(this, hit_tester_request.Pass());
}

void RendererImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  engine_->GetDisplayInfo(state_, [callback](mozart::DisplayInfoPtr info) {
    callback.Run(std::move(info));
  });
}

void RendererImpl::SetRootScene(mozart::SceneTokenPtr scene_token,
                                uint32_t scene_version,
                                mojo::RectPtr viewport) {
  engine_->SetRootScene(state_, scene_token.Pass(), scene_version,
                        viewport.Pass());
}

void RendererImpl::ClearRootScene() {
  engine_->ClearRootScene(state_);
}

void RendererImpl::GetScheduler(
    mojo::InterfaceRequest<mozart::FrameScheduler> scheduler_request) {
  scheduler_bindings_.AddBinding(this, scheduler_request.Pass());
}

void RendererImpl::ScheduleFrame(const ScheduleFrameCallback& callback) {
  engine_->ScheduleFrame(state_, [callback](mozart::FrameInfoPtr info) {
    callback.Run(std::move(info));
  });
}

void RendererImpl::HitTest(mojo::PointFPtr point,
                           const HitTestCallback& callback) {
  engine_->HitTest(state_, point.Pass(), callback);
}

}  // namespace compositor
