// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/scene_impl.h"

#include <utility>

namespace compositor {

SceneImpl::SceneImpl(CompositorEngine* engine,
                     SceneState* state,
                     fidl::InterfaceRequest<mozart::Scene> scene_request)
    : engine_(engine),
      state_(state),
      scene_binding_(this, std::move(scene_request)) {}

SceneImpl::~SceneImpl() {}

void SceneImpl::SetListener(
    fidl::InterfaceHandle<mozart::SceneListener> listener) {
  engine_->SetListener(state_,
                       mozart::SceneListenerPtr::Create(std::move(listener)));
}

void SceneImpl::Update(mozart::SceneUpdatePtr update) {
  engine_->Update(state_, std::move(update));
}

void SceneImpl::Publish(mozart::SceneMetadataPtr metadata) {
  engine_->Publish(state_, std::move(metadata));
}

void SceneImpl::GetScheduler(
    fidl::InterfaceRequest<mozart::FrameScheduler> scheduler_request) {
  scheduler_bindings_.AddBinding(this, std::move(scheduler_request));
}

void SceneImpl::ScheduleFrame(const ScheduleFrameCallback& callback) {
  engine_->ScheduleFrame(state_, [callback](mozart::FrameInfoPtr info) {
    callback(std::move(info));
  });
}

}  // namespace compositor
