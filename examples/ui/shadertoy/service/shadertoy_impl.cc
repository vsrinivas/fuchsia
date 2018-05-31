// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/shadertoy_impl.h"

namespace shadertoy {

ShadertoyImpl::ShadertoyImpl(fxl::RefPtr<ShadertoyState> state)
    : state_(std::move(state)) {}

ShadertoyImpl::~ShadertoyImpl() = default;

void ShadertoyImpl::SetPaused(bool paused) { state_->SetPaused(paused); }

void ShadertoyImpl::SetShaderCode(::fidl::StringPtr glsl,
                                  ::fuchsia::ui::shadertoy::Shadertoy::SetShaderCodeCallback callback) {
  state_->SetShaderCode(std::string(glsl), callback);
}

void ShadertoyImpl::SetResolution(uint32_t width, uint32_t height) {
  state_->SetResolution(width, height);
}

void ShadertoyImpl::SetMouse(fuchsia::ui::gfx::vec4 i_mouse) {
  state_->SetMouse(glm::vec4(i_mouse.x, i_mouse.y, i_mouse.z, i_mouse.w));
}

void ShadertoyImpl::SetImage(
    uint32_t channel, ::fidl::InterfaceRequest<fuchsia::images::ImagePipe> request) {
  state_->SetImage(channel, std::move(request));
}

}  // namespace shadertoy
