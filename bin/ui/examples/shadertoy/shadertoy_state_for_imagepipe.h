// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/shadertoy_state.h"

// Subclass of ShadertoyState that renders to an ImagePipe.
class ShadertoyStateForImagePipe : public ShadertoyState {
 public:
  ShadertoyStateForImagePipe(
      ShadertoyApp* app,
      ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe);
  ~ShadertoyStateForImagePipe();

 private:
  void OnSetResolution() override;
  escher::Framebuffer* GetOutputFramebuffer() override;

  ::fidl::InterfaceHandle<mozart2::ImagePipe> image_pipe_;
  static constexpr uint32_t kNumImages = 3;
  mx::vmo vmos_[kNumImages];
  escher::FramebufferPtr framebuffers_[kNumImages];
};
