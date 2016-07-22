// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"

#include "escher/gl/frame_buffer.h"
#include "escher/gl/texture_cache.h"
#include "escher/gl/unique_buffer.h"
#include "escher/gl/unique_program.h"
#include "escher/scene/stage.h"

namespace escher {

// Performs a depth-dependent blur on |color|.  Fragments that are farther away
// from the "blur plane" are blurred more.
// TODO(jjosh): The blur plane height is hardcoded in the fragment shader.
class DepthBasedBlurEffect {
 public:
  DepthBasedBlurEffect();
  ~DepthBasedBlurEffect();

  bool Init(TextureCache* texture_cache);
  void Draw(const Stage& stage,
            const Texture& color,
            const Texture& depth,
            float blur_plane_height,
            GLuint frame_buffer_id);

 private:
  TextureCache* texture_cache_ = nullptr;
  FrameBuffer frame_buffer_;

  UniqueProgram program_;
  GLint position_ = -1;
  GLint color_ = -1;
  GLint depth_ = -1;
  GLint tap_ = -1;
  GLint height_converter_ = -1;
  GLint blur_plane_height_ = -1;
  UniqueBuffer vertex_buffer_;
  UniqueBuffer index_buffer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DepthBasedBlurEffect);
};

}  // namespace escher
