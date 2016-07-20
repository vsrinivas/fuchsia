// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer.h"

#include <iostream>
#include <math.h>
#include <utility>

#include "escher/geometry/quad.h"
#include "escher/gl/bindings.h"
#include "escher/rendering/canvas.h"

#include "ftl/logging.h"

namespace escher {
namespace {

constexpr bool kDebugIllumination = false;
constexpr bool kDepthBasedBlur = true;
constexpr bool kUseMipmap = true;

}  // namespace

Renderer::Renderer() {}

Renderer::~Renderer() {}

bool Renderer::Init() {
  if (!blit_shader_.Compile()) return false;
  if (!lighting_.Init(&texture_cache_, kUseMipmap)) return false;
  if (!blur_.Init(&texture_cache_)) return false;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);

  unlit_scene_ = FrameBuffer::Make();
  lit_scene_ = FrameBuffer::Make();

  return true;
}

void Renderer::Render(const Stage& stage, const Model& model) {
  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  auto& size = stage.physical_size();
  glViewport(0, 0, size.width(), size.height());
  unlit_scene_.Bind();
  if (!unlit_scene_.color().size().Equals(size)) {
    unlit_scene_.SetDepth(texture_cache_.GetDepthTexture(size));
    unlit_scene_.SetColor(texture_cache_.GetColorTexture(size));
  }

  model_renderer_.DrawModel(stage, model);

  lighting_.Prepare(stage, unlit_scene_.depth());

  if (kDebugIllumination) {
    glBindFramebuffer(GL_FRAMEBUFFER, front_frame_buffer_id_);
    glClear(GL_COLOR_BUFFER_BIT);
    Blit(lighting_.illumination().id());
  } else if (kDepthBasedBlur && model.blur_plane_height() > 0.0) {
    lit_scene_.Bind();

    if (!lit_scene_.color().size().Equals(size)) {
      lit_scene_.SetColor(kUseMipmap ?
          texture_cache_.GetMipmappedColorTexture(size) :
          texture_cache_.GetColorTexture(size));
    }

    glClear(GL_COLOR_BUFFER_BIT);
    lighting_.Draw(unlit_scene_.color());

    if (kUseMipmap)
      GenerateMipmap(lit_scene_.color().id());

    blur_.Draw(stage,
               lit_scene_.color(),
               unlit_scene_.depth(),
               model.blur_plane_height(),
               front_frame_buffer_id_);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, front_frame_buffer_id_);
    glViewport(stage.viewport_offset().width(),
               stage.viewport_offset().height(),
               size.width(),
               size.height());
    glClear(GL_COLOR_BUFFER_BIT);
    lighting_.Draw(unlit_scene_.color());
  }

  FTL_DCHECK(glGetError() == GL_NO_ERROR);
}

void Renderer::Blit(GLuint texture_id) {
  glUseProgram(blit_shader_.program().id());
  glUniform1i(blit_shader_.source(), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glEnableVertexAttribArray(blit_shader_.position());
  DrawQuad(blit_shader_.position(), Quad::CreateFillClipSpace(0.0f));
}

void Renderer::GenerateMipmap(GLuint texture_id) const {
  glPushGroupMarkerEXT(25, "Renderer::GenerateMipmap");
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glGenerateMipmap(GL_TEXTURE_2D);
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    std::cerr << "Renderer::GenerateMipmap() failed: "
              << gl_error << std::endl;
    FTL_DCHECK(false);
  }
  glPopGroupMarkerEXT();
}

}  // namespace escher
