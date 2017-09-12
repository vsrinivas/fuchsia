// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/app.h"
#include "lib/ui/sketchy/types.h"

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

  CubicBezier2 curve1({1300.f, 520.f},
                      {1200.f, 570.f},
                      {1200.f, 620.f},
                      {1200.f, 670.f});
  CubicBezier2 curve2({1200.f, 720.f},
                      {1200.f, 770.f},
                      {1200.f, 820.f},
                      {1100.f, 870.f});
  StrokePath path({curve1, curve2});
  Stroke stroke(canvas_.get());
  stroke.SetPath(path);

  StrokeGroup group(canvas_.get());
  group.AddStroke(stroke);
  ImportNode node(canvas_.get(), scene_->stroke_group_holder());
  node.AddChild(group);

  uint64_t time = zx_time_get(ZX_CLOCK_MONOTONIC);
  canvas_->Present(time);
  session_->Present(time, [](scenic::PresentationInfoPtr info) {});
}

}  // namespace sketchy_example
