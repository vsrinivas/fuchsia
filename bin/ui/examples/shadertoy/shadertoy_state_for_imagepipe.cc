// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/shadertoy_state_for_imagepipe.h"

#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

ShadertoyStateForImagePipe::ShadertoyStateForImagePipe(
    ShadertoyApp* app,
    ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe)
    : ShadertoyState(app), image_pipe_(std::move(image_pipe)) {
  FTL_CHECK(false) << "not implemented";
}

ShadertoyStateForImagePipe::~ShadertoyStateForImagePipe() = default;

void ShadertoyStateForImagePipe::OnSetResolution() {
  if (framebuffers_[0]) {
    FTL_CHECK(false) << "no support for changing resolution.";
  }

  escher::ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Srgb;
  info.width = width();
  info.height = height();
  info.sample_count = 0;

  FTL_CHECK(false) << "not implemented";
}

escher::Framebuffer* ShadertoyStateForImagePipe::GetOutputFramebuffer() {
  FTL_CHECK(false) << "not implemented";
  return nullptr;
}
