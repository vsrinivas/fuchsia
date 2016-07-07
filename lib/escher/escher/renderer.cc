// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer.h"

#include <iostream>
#include <math.h>
#include <utility>

#include "escher/gl/bindings.h"
#include "escher/rendering/canvas.h"

namespace escher {
namespace {

constexpr bool kDebugIllumination = false;

}  // namespace

Renderer::Renderer() {}

Renderer::~Renderer() {}

bool Renderer::Init() {
  if (!blit_shader_.Compile() || !lighting_.Init(&texture_cache_))
    return false;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);

  scene_ = FrameBuffer::Make();
  return true;
}

void Renderer::Render(const Stage& stage, const Model& model) {
  auto& size = stage.physical_size();

  glViewport(0, 0, size.width(), size.height());
  scene_.Bind();

  if (!scene_.color().size().Equals(size)) {
    scene_.SetDepth(texture_cache_.GetDepthTexture(size));
    scene_.SetColor(texture_cache_.GetColorTexture(size));
  }

  model_renderer_.DrawModel(stage, model);

  lighting_.Prepare(stage, scene_.depth());

  glBindFramebuffer(GL_FRAMEBUFFER, front_frame_buffer_id_);
  glClear(GL_COLOR_BUFFER_BIT);
  if (kDebugIllumination)
    Blit(lighting_.illumination().id());
  else
    lighting_.Draw(scene_.color());
}

void Renderer::Blit(GLuint texture_id) {
  glUseProgram(blit_shader_.program().id());
  glUniform1i(blit_shader_.source(), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glEnableVertexAttribArray(blit_shader_.position());
  DrawQuad(blit_shader_.position(), Quad::CreateFillClipSpace(0.0f));
}

}  // namespace escher
