// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/tests/mocks/mock_renderer.h"

#include "lib/mtl/tasks/message_loop.h"
#include "lib/ftl/time/time_delta.h"

namespace view_manager {
namespace test {

MockRenderer::MockRenderer() {}
MockRenderer::~MockRenderer() {}

void MockRenderer::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  mozart::DisplayInfoPtr display_info = mozart::DisplayInfo::New();
  display_info->size = mozart::Size::New();
  display_info->size->width = 800;
  display_info->size->height = 600;
  callback(std::move(display_info));
}

void MockRenderer::SetRootScene(mozart::SceneTokenPtr scene_token,
                                uint32_t scene_version,
                                mozart::RectPtr viewport) {
  scene_token_ = std::move(scene_token);
  scene_version_ = scene_version;
  viewport_ = std::move(viewport);
}

void MockRenderer::ClearRootScene() {
  scene_token_.reset();
  scene_version_ = 0;
  viewport_.reset();
}

void MockRenderer::GetScheduler(
    fidl::InterfaceRequest<mozart::FrameScheduler> scheduler_request) {
  scheduler_bindings_.AddBinding(this, std::move(scheduler_request));
}

void MockRenderer::GetHitTester(
    fidl::InterfaceRequest<mozart::HitTester> hit_tester_request) {
  hit_tester_bindings.AddBinding(this, std::move(hit_tester_request));
}

void MockRenderer::ScheduleFrame(const ScheduleFrameCallback& callback) {
  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([callback]() {
    ftl::TimePoint now = ftl::TimePoint::Now();

    mozart::FrameInfoPtr info = mozart::FrameInfo::New();
    ftl::TimeDelta interval = ftl::TimeDelta::FromMilliseconds(16);
    info->presentation_interval = interval.ToNanoseconds();
    info->presentation_time = (now + interval).ToEpochDelta().ToNanoseconds();
    info->publish_deadline =
        (now + interval / 2).ToEpochDelta().ToNanoseconds();
    info->base_time = now.ToEpochDelta().ToNanoseconds();
    callback(std::move(info));
  });
}

void MockRenderer::HitTest(mozart::PointFPtr point,
                           const HitTestCallback& callback) {
  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([callback]() {
    auto result = mozart::HitTestResult::New();
    callback(std::move(result));
  });
}

}  // namespace test
}  // namespace view_manager
