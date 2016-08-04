// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer.h"

#include <iostream>
#include <math.h>
#include <utility>

#include "escher/geometry/quad.h"
#include "escher/gl/gles2/bindings.h"
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
  if (!model_renderer_.Init()) return false;
  if (!blit_shader_.Compile()) return false;
  if (!lighting_.Init(&texture_cache_, kUseMipmap)) return false;
  if (!blur_.Init(&texture_cache_)) return false;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);

  return true;
}

void Renderer::Render(const Stage& stage, const Model& model) {
  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  auto& size = stage.physical_size();
  glViewport(0, 0, size.width(), size.height());

  // Draw unlit model.
  Texture unlit_color, unlit_depth;
  {
    RenderPassSpec pass_spec;
    pass_spec.color[0].texture = texture_cache_.GetColorTexture(size);
    pass_spec.depth.texture = texture_cache_.GetDepthTexture(size);

    context_.BeginRenderPass(pass_spec, "Renderer::Render draw model");
    model_renderer_.DrawModel(stage, model);
    context_.EndRenderPass();

    unlit_color = std::move(pass_spec.color[0].texture);
    unlit_depth = std::move(pass_spec.depth.texture);
  }

  Texture illumination = texture_cache_.GetColorTexture(size);
  Texture lit_color = kUseMipmap ?
      texture_cache_.GetMipmappedColorTexture(size) :
      texture_cache_.GetColorTexture(size);
  lighting_.Apply(
      stage, &context_, unlit_color, unlit_depth, illumination, lit_color);

  if (kUseMipmap)
    GenerateMipmap(lit_color.id());

  Texture finished;
  if (kDebugIllumination) {
    finished = std::move(illumination);
  } else if (kDepthBasedBlur && model.blur_plane_height() > 0.0) {
    finished = texture_cache_.GetMipmappedColorTexture(size);
    blur_.Draw(stage,
               &context_,
               lit_color,
               unlit_depth,
               finished,
               model.blur_plane_height());
  } else {
    finished = std::move(lit_color);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, front_frame_buffer_id_);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(stage.viewport_offset().width(),
             stage.viewport_offset().height(),
             size.width(),
             size.height());
  Blit(finished.id());

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
