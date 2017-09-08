// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

namespace shadertoy {

// Subclass of ShadertoyState that renders to an ImagePipe.
class ShadertoyStateForImagePipe : public ShadertoyState {
 public:
  ShadertoyStateForImagePipe(
      App* app,
      ::fidl::InterfaceHandle<scenic::ImagePipe> image_pipe);
  ~ShadertoyStateForImagePipe();

 private:
  // |ShadertoyState|
  void OnSetResolution() override;

  // |ShadertoyState|
  void DrawFrame(uint64_t presentation_time, float animation_time) override;

  void ClearFramebuffers();

  struct Framebuffer {
    escher::FramebufferPtr framebuffer;
    escher::SemaphorePtr acquire_semaphore;
    escher::SemaphorePtr release_semaphore;
    // Signaled by Renderer when frame is finished, and therefore ready for the
    // ImagePipe consumer to use.
    mx::event acquire_fence;
    // Signaled by the ImagePipe consumer when the framebuffer is no longer used
    // and can therefore be rendered into.
    mx::event release_fence;
    uint32_t image_pipe_id = 0;
  };
  static constexpr uint32_t kNumFramebuffers = 2;
  Framebuffer framebuffers_[kNumFramebuffers];

  // ImagePipe that we produce images for.
  scenic::ImagePipePtr image_pipe_;
  // Next ID to use when calling ImagePipe.AddImage().
  uint32_t next_image_pipe_id_ = 1;
  // Index of framebuffer to use the next time OnDrawFrame() is called.
  uint32_t next_framebuffer_index_ = 0;
};

}  // namespace shadertoy
