// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/app.h"
#include "lib/ui/sketchy/types.h"

namespace {

// Mock a path of wave starting from |start| with |seg_count| segments.
sketchy_lib::StrokePath MockWavePath(glm::vec2 start, uint32_t seg_count) {
  std::vector<sketchy_lib::CubicBezier2> segments;
  segments.reserve(seg_count);
  for (uint32_t i = 0; i < seg_count; i++) {
    segments.push_back({{start},
                        {start + glm::vec2{40, 0}},
                        {start + glm::vec2{40, 40}},
                        {start + glm::vec2{80, 0}}});
    start += glm::vec2{80, 0};
  }
  return segments;
}

}  // namespace

namespace sketchy_example {

App::App()
    : loop_(fsl::MessageLoop::GetCurrent()),
      context_(app::ApplicationContext::CreateFromStartupInfo()),
      scene_manager_(
          context_->ConnectToEnvironmentService<scenic::SceneManager>()),
      session_(std::make_unique<scenic_lib::Session>(scene_manager_.get())),
      canvas_(std::make_unique<Canvas>(context_.get())) {
  session_->set_connection_error_handler([this] {
    FXL_LOG(INFO) << "sketchy_example: lost connection to scenic::Session.";
    loop_->QuitNow();
  });
  scene_manager_.set_connection_error_handler([this] {
    FXL_LOG(INFO)
        << "sketchy_example: lost connection to scenic::SceneManager.";
    loop_->QuitNow();
  });
  scene_manager_->GetDisplayInfo([this](scenic::DisplayInfoPtr display_info) {
    Init(std::move(display_info));
  });
}

void App::Init(scenic::DisplayInfoPtr display_info) {
  scene_ = std::make_unique<Scene>(session_.get(), display_info->physical_width,
                                   display_info->physical_height);

  CubicBezier2 curve1({1180.f, 540.f},
                      {1080.f, 540.f},
                      {1080.f, 640.f},
                      {1080.f, 690.f});
  CubicBezier2 curve2({1080.f, 750.f},
                      {1080.f, 800.f},
                      {1080.f, 900.f},
                      {980.f, 900.f});
  StrokePath path1({curve1, curve2});
  Stroke stroke1(canvas_.get());
  stroke1.SetPath(path1);

  CubicBezier2 curve3({980.f, 720.f},
                      {1040.f, 720.f},
                      {1120.f, 720.f},
                      {1180.f, 720.f});
  StrokePath path2({curve3});
  Stroke stroke2(canvas_.get());
  stroke2.SetPath(path2);

  StrokeGroup group(canvas_.get());
  group.AddStroke(stroke1);
  group.AddStroke(stroke2);

  // Draw waves
  for (uint32_t i = 0; i < 4; i++) {
    glm::vec2 start = {50, 50 + i * 100};
    StrokePath path = MockWavePath(start, 26);
    Stroke stroke(canvas_.get());
    stroke.SetPath(path);
    group.AddStroke(stroke);
  }

  ImportNode node(canvas_.get(), scene_->stroke_group_holder());
  node.AddChild(group);

  uint64_t time = zx_time_get(ZX_CLOCK_MONOTONIC);
  canvas_->Present(time, [](scenic::PresentationInfoPtr info) {});
  session_->Present(time, [](scenic::PresentationInfoPtr info) {});
}

}  // namespace sketchy_example
