// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/client/app.h"

#include "apps/mozart/examples/shadertoy/client/glsl_strings.h"
#include "apps/mozart/lib/scene/session_helpers.h"

namespace shadertoy_client {

App::App()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      loop_(mtl::MessageLoop::GetCurrent()),
      scene_manager_(
          application_context_
              ->ConnectToEnvironmentService<mozart2::SceneManager>()),
      session_(scene_manager_.get()),
      // TODO: we don't need to keep this around once we have used it to
      // create a Shadertoy.  What is the best way to achieve this?
      shadertoy_factory_(application_context_->ConnectToEnvironmentService<
                         mozart::example::ShadertoyFactory>()) {
  scene_manager_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to SceneManager.";
    loop_->QuitNow();
  });
  session_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to Session.";
    loop_->QuitNow();
  });
  shadertoy_factory_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to ShadertoyFactory.";
    loop_->QuitNow();
  });
  scene_manager_->GetDisplayInfo([this](mozart2::DisplayInfoPtr display_info) {
    Init(std::move(display_info));
  });
}

void App::Init(mozart2::DisplayInfoPtr display_info) {
  // Create an ImagePipe and pass one end of it to the ShadertoyFactory in
  // order to obtain a Shadertoy.
  fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe_handle;
  auto image_pipe_request = image_pipe_handle.NewRequest();
  shadertoy_factory_->NewImagePipeShadertoy(shadertoy_.NewRequest(),
                                            std::move(image_pipe_handle));
  shadertoy_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "Lost connection to Shadertoy.";
    loop_->QuitNow();
  });

  // Set the GLSL source code for the Shadertoy.
  constexpr uint32_t kWidth = 512;
  constexpr uint32_t kHeight = 384;
  shadertoy_->SetResolution(kWidth, kHeight);
  shadertoy_->SetShaderCode(GetSeascapeSourceCode(), [this](bool success) {
    if (success) {
      FTL_LOG(INFO) << "GLSL code was successfully compiled.";
      shadertoy_->SetPaused(false);
    } else {
      FTL_LOG(ERROR) << "GLSL code compilation failed";
      loop_->QuitNow();
    }
  });

  // Pass the other end of the ImagePipe to the Session, and wrap the
  // resulting resource in a Material.
  uint32_t image_pipe_id = session_.AllocResourceId();
  session_.Enqueue(mozart::NewCreateImagePipeOp(image_pipe_id,
                                                std::move(image_pipe_request)));
  mozart::client::Material material(&session_);
  material.SetTexture(image_pipe_id);
  session_.ReleaseResource(image_pipe_id);

  const float scene_width = static_cast<float>(display_info->physical_width);
  const float scene_height = static_cast<float>(display_info->physical_height);
  const float rect_width = static_cast<float>(kWidth);
  const float rect_height = static_cast<float>(kHeight);
  scene_ = std::make_unique<ExampleScene>(
      &session_, material, scene_width, scene_height, rect_width, rect_height);

  start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
  Update(start_time_);
}

void App::Update(uint64_t next_presentation_time) {
  // Translate the rounded rect.
  double secs =
      static_cast<double>(next_presentation_time - start_time_) / 1'000'000'000;

  scene_->Update(secs);

  // Present
  session_.Present(
      next_presentation_time, [this](mozart2::PresentationInfoPtr info) {
        Update(info->presentation_time + info->presentation_interval);
      });
}

}  // namespace shadertoy_client
