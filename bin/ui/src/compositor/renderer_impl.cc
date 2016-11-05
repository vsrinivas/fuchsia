// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/renderer_impl.h"

namespace compositor {

RendererImpl::RendererImpl(
    CompositorEngine* engine,
    RendererState* state,
    fidl::InterfaceRequest<mozart::Renderer> renderer_request)
    : engine_(engine),
      state_(state),
      renderer_binding_(this, std::move(renderer_request)) {}

RendererImpl::~RendererImpl() {}

void RendererImpl::GetHitTester(
    fidl::InterfaceRequest<mozart::HitTester> hit_tester_request) {
  hit_tester_bindings.AddBinding(this, std::move(hit_tester_request));
}

void RendererImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  engine_->GetDisplayInfo(state_, [callback](mozart::DisplayInfoPtr info) {
    callback(std::move(info));
  });
}

void RendererImpl::SetRootScene(mozart::SceneTokenPtr scene_token,
                                uint32_t scene_version,
                                mozart::RectPtr viewport) {
  engine_->SetRootScene(state_, std::move(scene_token), scene_version,
                        std::move(viewport));
}

void RendererImpl::ClearRootScene() {
  engine_->ClearRootScene(state_);
}

void RendererImpl::GetScheduler(
    fidl::InterfaceRequest<mozart::FrameScheduler> scheduler_request) {
  scheduler_bindings_.AddBinding(this, std::move(scheduler_request));
}

void RendererImpl::ScheduleFrame(const ScheduleFrameCallback& callback) {
  engine_->ScheduleFrame(state_, [callback](mozart::FrameInfoPtr info) {
    callback(std::move(info));
  });
}

void RendererImpl::HitTest(mozart::PointFPtr point,
                           const HitTestCallback& callback) {
  engine_->HitTest(state_, std::move(point), callback);
}

}  // namespace compositor
