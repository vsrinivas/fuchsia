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
#include "escher/gl/context.h"
#include "escher/gl/texture.h"
#include "escher/gl/texture_cache.h"
#include "escher/scene/stage.h"

namespace escher {

class LightingEffect {
 public:
  LightingEffect();
  ~LightingEffect();

  bool Init(TextureCache* texture_cache, bool use_mipmap);

  void Apply(
      const Stage& stage, Context* context,
      const Texture& unlit_color, const Texture& unlit_depth,
      const Texture& illumination_out, const Texture& lit_color);

 private:
  void GenerateIllumination(
      const Stage& stage, Context* context,
      const Texture& unlit_color, const Texture& unlit_depth,
      const Texture& illumination_out);
  void FilterIllumination(
      const Stage& stage, Context* context,
      const Texture& illumination_out);
  void ApplyIllumination(Context* context,
                         const Texture& unlit_color,
                         const Texture& illumination_out,
                         const Texture& lit_color);

  TextureCache* texture_cache_ = nullptr;
  IlluminationShader shader_;
  IlluminationReconstructionFilter blur_;
  OcclusionDetector occlusion_detector_;
  Quad full_frame_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LightingEffect);
};

}  // namespace escher
