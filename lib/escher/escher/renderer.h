// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/base/macros.h"
#include "escher/base/time.h"
#include "escher/geometry/quad.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/frame_buffer.h"
#include "escher/rendering/model_renderer.h"
#include "escher/scene/stage.h"
#include "escher/scene/model.h"
#include "escher/shaders/blit_shader.h"
#include "escher/shaders/lighting/illumination_shader.h"
#include "escher/shaders/lighting/illumination_reconstruction_filter.h"
#include "escher/shaders/lighting/occlusion_detector.h"

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
  void ResizeBuffers(const SizeI& size);
  void Blit(GLuint texture_id);
  void DrawModel(const Model& model, const glm::mat4& matrix);

  void ComputeIllumination(const Stage& stage);
  void DrawFullFrameQuad(GLint position);

  GLuint front_frame_buffer_id_ = 0;
  SizeI size_;
  FrameBuffer scene_buffer_;
  FrameBuffer lighting_buffer_;
  UniqueTexture scratch_texture_;
  Quad full_frame_;

  BlitShader blit_shader_;
  IlluminationShader illumination_shader_;
  IlluminationReconstructionFilter reconstruction_filter_;
  OcclusionDetector occlusion_detector_;
  ModelRenderer model_renderer_;

  UniqueTexture noise_texture_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher
