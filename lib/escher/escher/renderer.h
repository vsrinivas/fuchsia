// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "ftl/macros.h"
#include "escher/effects/lighting/lighting_effect.h"
#include "escher/geometry/quad.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/frame_buffer.h"
#include "escher/gl/texture_cache.h"
#include "escher/rendering/model_renderer.h"
#include "escher/scene/stage.h"
#include "escher/scene/model.h"
#include "escher/shaders/blit_shader.h"

namespace escher {

class Renderer {
 public:
  Renderer();
  ~Renderer();

  bool Init();

  GLuint front_frame_buffer_id() const { return front_frame_buffer_id_; }
  void set_front_frame_buffer_id(GLuint value) {
    front_frame_buffer_id_ = value;
  }

  void Render(const Stage& stage, const Model& model);

 private:
  void Blit(GLuint texture_id);

  GLuint front_frame_buffer_id_ = 0;
  TextureCache texture_cache_;
  LightingEffect lighting_;
  BlitShader blit_shader_;
  SizeI size_;
  FrameBuffer scene_;
  Quad full_frame_;

  ModelRenderer model_renderer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher
