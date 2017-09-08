// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

#include "garnet/examples/ui/shadertoy/service/app.h"
#include "garnet/examples/ui/shadertoy/service/imagepipe_shadertoy.h"
#include "garnet/examples/ui/shadertoy/service/pipeline.h"
#include "garnet/examples/ui/shadertoy/service/renderer.h"
#include "garnet/examples/ui/shadertoy/service/view_shadertoy.h"

namespace shadertoy {

ftl::RefPtr<ShadertoyState> ShadertoyState::NewForImagePipe(
    App* app,
    ::fidl::InterfaceHandle<scenic::ImagePipe> image_pipe) {
  return ftl::AdoptRef(
      new ShadertoyStateForImagePipe(app, std::move(image_pipe)));
}

ftl::RefPtr<ShadertoyState> ShadertoyState::NewForView(
    App* app,
    ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    bool handle_input_events) {
  FTL_CHECK(false) << "unimplemented.";
  return ftl::RefPtr<ShadertoyState>();
#if 0
  return ftl::AdoptRef(new ShadertoyStateForView(
      app, std::move(view_owner_request), handle_input_events));
#endif
}

ShadertoyState::ShadertoyState(App* app)
    : app_(app),
      escher_(app_->escher()),
      compiler_(app_->compiler()),
      renderer_(app_->renderer()),
      weak_ptr_factory_(this),
      stopwatch_(false) {}

ShadertoyState::~ShadertoyState() = default;

void ShadertoyState::SetPaused(bool paused) {
  is_paused_ = paused;
  if (is_paused_) {
    stopwatch_.Stop();
  } else {
    stopwatch_.Start();
  }
  RequestFrame(0);
}

void ShadertoyState::SetShaderCode(
    std::string glsl,
    const mozart::example::Shadertoy::SetShaderCodeCallback& callback) {
  compiler_->Compile(std::string(glsl), [
    weak = weak_ptr_factory_.GetWeakPtr(), callback = callback
  ](Compiler::Result result) {
    if (weak) {
      if (result.pipeline) {
        // Notify client that the code was successfully compiled.
        callback(true);
        // Start rendering with the new pipeline.
        weak->pipeline_ = std::move(result.pipeline);
        weak->RequestFrame(0);
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
  RequestFrame(0);
}

void ShadertoyState::SetMouse(glm::vec4 i_mouse) {
  if (i_mouse != i_mouse_) {
    i_mouse_ = i_mouse;
    RequestFrame(0);
  }
}

void ShadertoyState::SetImage(
    uint32_t channel,
    ::fidl::InterfaceRequest<scenic::ImagePipe> request) {
  FTL_CHECK(false) << "unimplemented";
}

void ShadertoyState::RequestFrame(uint64_t presentation_time) {
  if (is_drawing_ || is_paused_ || !pipeline_ || (width_ * height_ == 0)) {
    return;
  }
  is_drawing_ = true;

  // The stars have aligned; draw a frame.
  DrawFrame(presentation_time, stopwatch_.GetElapsedSeconds());
}

void ShadertoyState::OnFramePresented(const scenic::PresentationInfoPtr& info) {
  FTL_DCHECK(is_drawing_);
  is_drawing_ = false;
  RequestFrame(info->presentation_time + info->presentation_interval);
}

void ShadertoyState::Close() {
  FTL_CHECK(false) << "unimplemented";
}

}  // namespace shadertoy
