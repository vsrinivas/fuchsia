// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "ftl/macros.h"
#include "escher/effects/lighting/illumination_reconstruction_filter.h"
#include "escher/effects/lighting/illumination_shader.h"
#include "escher/effects/lighting/occlusion_detector.h"
#include "escher/geometry/quad.h"
#include "escher/gl/frame_buffer.h"
#include "escher/gl/texture_cache.h"
#include "escher/scene/stage.h"

namespace escher {

class LightingEffect {
 public:
  LightingEffect();
  ~LightingEffect();

  bool Init(TextureCache* texture_cache, bool use_mipmap);

  void Prepare(const Stage& stage, const Texture& depth);
  void Draw(const Texture& color);

  const Texture& illumination() const { return frame_buffer_.color(); }

 private:
  void GenerateMipmap(GLuint texture_id) const;
  
  TextureCache* texture_cache_ = nullptr;
  bool use_mipmap_ = false;
  FrameBuffer frame_buffer_;
  IlluminationShader shader_;
  IlluminationReconstructionFilter blur_;
  OcclusionDetector occlusion_detector_;
  Quad full_frame_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LightingEffect);
};

}  // namespace escher
