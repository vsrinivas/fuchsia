// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/sketchy/app.h"

namespace sketchy_example {

App::App()
    : loop_(mtl::MessageLoop::GetCurrent()),
      context_(app::ApplicationContext::CreateFromStartupInfo()),
      scene_manager_(
          context_->ConnectToEnvironmentService<mozart2::SceneManager>()),
      session_(std::make_unique<mozart::client::Session>(scene_manager_.get())),
      canvas_(std::make_unique<Canvas>(context_.get())) {
  session_->set_connection_error_handler([this] {
    FTL_LOG(INFO)
        << "sketchy_example: lost connection to scenic::Session.";
    loop_->QuitNow();
  });
  scene_manager_.set_connection_error_handler([this] {
    FTL_LOG(INFO)
        << "sketchy_example: lost connection to scenic::SceneManager.";
    loop_->QuitNow();
  });
  scene_manager_->GetDisplayInfo([this](mozart2::DisplayInfoPtr display_info) {
    Init(std::move(display_info));
  });
}

void App::Init(mozart2::DisplayInfoPtr display_info) {
  scene_ = std::make_unique<Scene>(
      session_.get(),
      display_info->physical_width,
      display_info->physical_height);

  ResourceId node = canvas_->ImportNode(scene_->stroke_group_holder());
  ResourceId stroke = canvas_->resources()->CreateStroke();
  canvas_->AddStrokeToNode(stroke, node);

  uint64_t time = mx_time_get(MX_CLOCK_MONOTONIC);
  canvas_->Present(time);
  session_->Present(time, [](mozart2::PresentationInfoPtr info){});
}

}  // namespace sketchy_example
