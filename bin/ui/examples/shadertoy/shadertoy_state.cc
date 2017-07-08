// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/shadertoy_state.h"

#include "apps/mozart/examples/shadertoy/pipeline.h"
#include "apps/mozart/examples/shadertoy/renderer.h"
#include "apps/mozart/examples/shadertoy/shadertoy_app.h"
#include "apps/mozart/examples/shadertoy/shadertoy_state_for_imagepipe.h"
#include "apps/mozart/examples/shadertoy/shadertoy_state_for_material.h"
#include "apps/mozart/examples/shadertoy/shadertoy_state_for_view.h"

ftl::RefPtr<ShadertoyState> ShadertoyState::NewForImagePipe(
    ShadertoyApp* app,
    ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe) {
  return ftl::AdoptRef(
      new ShadertoyStateForImagePipe(app, std::move(image_pipe)));
}

ftl::RefPtr<ShadertoyState> ShadertoyState::NewForMaterial(
    ShadertoyApp* app,
    mx::eventpair export_token) {
  return ftl::AdoptRef(
      new ShadertoyStateForMaterial(app, std::move(export_token)));
}

ftl::RefPtr<ShadertoyState> ShadertoyState::NewForView(
    ShadertoyApp* app,
    ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    bool handle_input_events) {
  return ftl::AdoptRef(new ShadertoyStateForView(
      app, std::move(view_owner_request), handle_input_events));
}

ShadertoyState::ShadertoyState(ShadertoyApp* app)
    : app_(app),
      escher_(app_->escher()),
      compiler_(app_->compiler()),
      renderer_(app_->renderer()),
      weak_ptr_factory_(this) {}

ShadertoyState::~ShadertoyState() = default;

void ShadertoyState::SetPaused(bool paused) {
  FTL_CHECK(false) << "unimplemented";
}

void ShadertoyState::SetShaderCode(
    std::string glsl,
    const Shadertoy::SetShaderCodeCallback& callback) {
  compiler_->Compile(std::string(glsl), [
    weak = weak_ptr_factory_.GetWeakPtr(), callback = callback
  ](Compiler::Result result) {
    if (weak) {
      if (result.pipeline) {
        // Notify client that the code was successfully compiled.
        callback(true);
        // Start rendering with the new pipeline.
        weak->pipeline_ = std::move(result.pipeline);
        weak->RequestFrame();
      } else {
        // Notify client that the code could not be successfully compiled.
        callback(false);
      }
    }
  });
}

void ShadertoyState::SetResolution(uint32_t width, uint32_t height) {
  if (width == width_ && height == height_) {
    return;
  }
  if (width > kMaxWidth) {
    FTL_LOG(ERROR) << "Resolution max width exceeded, " << width << " > "
                   << kMaxWidth;
    return;
  }
  if (height > kMaxHeight) {
    FTL_LOG(ERROR) << "Resolution max height exceeded, " << height << " > "
                   << kMaxHeight;
    return;
  }

  width_ = width;
  height_ = height;
  OnSetResolution();
  RequestFrame();
}

void ShadertoyState::SetMouse(glm::vec4 i_mouse) {
  if (i_mouse != i_mouse_) {
    i_mouse_ = i_mouse;
    RequestFrame();
  }
}

void ShadertoyState::SetImage(
    uint32_t channel,
    ::fidl::InterfaceRequest<mozart2::ImagePipe> request) {
  FTL_CHECK(false) << "unimplemented";
}

void ShadertoyState::DrawFrame(uint64_t presentation_time) {
  if (!pipeline_) {
    FTL_LOG(WARNING)
        << "Frame should not have been scheduled without a pipeline available.";
    return;
  }

  if (width_ * height_ == 0) {
    FTL_LOG(WARNING) << "Skipping frame with width=0 and/or height=0.";
    return;
  }

  glm::vec4 i_mouse(1, 1, 1, 1);

  FTL_CHECK(false) << "frame time unimplemented.";
  float time = 0.f;

  escher::SemaphorePtr sema;
  FTL_CHECK(false) << "semaphore unimplemented.";

  renderer_->DrawFrame(pipeline_, GetOutputFramebuffer(), nullptr, nullptr,
                       nullptr, nullptr, i_mouse, time, std::move(sema));
}

void ShadertoyState::RequestFrame() {
  FTL_CHECK(false) << "unimplemented";
}

void ShadertoyState::Close() {
  FTL_CHECK(false) << "unimplemented";
}
