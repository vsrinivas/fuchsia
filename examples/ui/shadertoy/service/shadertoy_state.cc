// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

#include "garnet/examples/ui/shadertoy/service/app.h"
#include "garnet/examples/ui/shadertoy/service/imagepipe_shadertoy.h"
#include "garnet/examples/ui/shadertoy/service/pipeline.h"
#include "garnet/examples/ui/shadertoy/service/renderer.h"
#include "garnet/examples/ui/shadertoy/service/view_shadertoy.h"
#include "lib/escher/resources/resource_recycler.h"

namespace shadertoy {

fxl::RefPtr<ShadertoyState> ShadertoyState::NewForImagePipe(
    App* app, ::fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe) {
  return fxl::AdoptRef(
      new ShadertoyStateForImagePipe(app, std::move(image_pipe)));
}

fxl::RefPtr<ShadertoyState> ShadertoyState::NewForView(
    App* app, ::fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                  view_owner_request,
    bool handle_input_events) {
  FXL_CHECK(false) << "unimplemented.";
  return fxl::RefPtr<ShadertoyState>();
#if 0
  return fxl::AdoptRef(new ShadertoyStateForView(
      app, std::move(view_owner_request), handle_input_events));
#endif
}

ShadertoyState::ShadertoyState(App* app)
    : escher::Resource(app->escher()->resource_recycler()),
      app_(app),
      escher_(app_->escher()->GetWeakPtr()),
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
    fidl::StringPtr glsl,
    fuchsia::examples::shadertoy::Shadertoy::SetShaderCodeCallback callback) {
  compiler_->Compile(std::string(glsl), [
    weak = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)
  ](Compiler::Result result) {
    if (weak) {
      if (result.pipeline) {
        // Notify client that the code was successfully
        // compiled.
        callback(true);
        // Start rendering with the new pipeline.
        weak->pipeline_ = std::move(result.pipeline);
        weak->RequestFrame(0);
      } else {
        // Notify client that the code could not be
        // successfully compiled.
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
    FXL_LOG(ERROR) << "Resolution max width exceeded, " << width << " > "
                   << kMaxWidth;
    return;
  }
  if (height > kMaxHeight) {
    FXL_LOG(ERROR) << "Resolution max height exceeded, " << height << " > "
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
    ::fidl::InterfaceRequest<fuchsia::images::ImagePipe> request) {
  FXL_CHECK(false) << "unimplemented";
}

void ShadertoyState::RequestFrame(uint64_t presentation_time) {
  if (is_drawing_ || is_paused_ || is_closed_ || !pipeline_ ||
      (width_ * height_ == 0)) {
    return;
  }
  is_drawing_ = true;

  // The stars have aligned; draw a frame.
  DrawFrame(presentation_time, stopwatch_.GetElapsedSeconds());

  // Ensure that all frames are finished before this object is destroyed.
  KeepAlive(escher()->command_buffer_sequencer()->latest_sequence_number());
}

void ShadertoyState::OnFramePresented(fuchsia::images::PresentationInfo info) {
  FXL_DCHECK(is_drawing_);
  is_drawing_ = false;
  RequestFrame(info.presentation_time + info.presentation_interval);
}

void ShadertoyState::Close() {
  if (!is_closed_) {
    is_closed_ = true;
    KeepAlive(escher()->command_buffer_sequencer()->latest_sequence_number());
    app_->CloseShadertoy(this);
  }
}

}  // namespace shadertoy
