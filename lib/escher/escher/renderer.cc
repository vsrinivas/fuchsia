// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer.h"

#include <iostream>
#include <math.h>
#include <utility>

#include "escher/gl/bindings.h"
#include "escher/textures/noise_texture.h"

namespace escher {
namespace {

constexpr bool kIlluminateScene = true;
constexpr bool kFilterLightingBuffer = true;

}  // namespace

Renderer::Renderer()
    : scene_buffer_(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT),
      lighting_buffer_(GL_COLOR_BUFFER_BIT) {}

Renderer::~Renderer() {}

bool Renderer::Init() {
  if (!blit_shader_.Compile() || !illumination_shader_.Compile() ||
      !reconstruction_filter_.Compile() || !occlusion_detector_.Compile())
    return false;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);

  noise_texture_ = MakeNoiseTexture(
      SizeI(OcclusionDetector::kNoiseSize, OcclusionDetector::kNoiseSize));

  full_frame_ = Quad::CreateFromRect(glm::vec2(-1.0f, 1.0f),
                                     glm::vec2(2.0f, -2.0f), 0.0f);

  return true;
}

void Renderer::ResizeBuffers(const SizeI& size) {
  ESCHER_DCHECK(!size_.Equals(size));
  if (!scene_buffer_.SetSize(size) || !lighting_buffer_.SetSize(size)) {
    std::cerr << "Failed to allocate frame buffer of size (" << size.width()
              << ", " << size.height() << ")." << std::endl;
    exit(1);
  }

  scratch_texture_ = MakeColorTexture(size);
  size_ = size;
}

void Renderer::Render(const Stage& stage, const Model& model) {
  if (!size_.Equals(stage.size()))
    ResizeBuffers(stage.size());

  glm::mat4 matrix = stage.viewing_volume().GetProjectionMatrix();
  glViewport(0, 0, size_.width(), size_.height());

  model_renderer_.DrawModel(stage, model, matrix, scene_buffer_.frame_buffer());

  ComputeIllumination(stage);

  glBindFramebuffer(GL_FRAMEBUFFER, front_frame_buffer_id_);
  glClear(GL_COLOR_BUFFER_BIT);
  if (!kIlluminateScene) {
    Blit(lighting_buffer_.color().id());
  } else {
    glUseProgram(illumination_shader_.program().id());
    glUniform1i(illumination_shader_.scene(), 0);
    glUniform1i(illumination_shader_.lighting(), 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_buffer_.color().id());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lighting_buffer_.color().id());
    glEnableVertexAttribArray(illumination_shader_.position());
    DrawFullFrameQuad(illumination_shader_.position());
  }
}

void Renderer::ComputeIllumination(const Stage& stage) {
  glBindFramebuffer(GL_FRAMEBUFFER, lighting_buffer_.frame_buffer().id());
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(occlusion_detector_.program().id());
  glUniform1i(occlusion_detector_.depth_map(), 0);
  glUniform1i(occlusion_detector_.noise(), 1);
  auto& size = stage.size();
  glUniform3f(occlusion_detector_.viewing_volume(), size.width(), size.height(),
              stage.viewing_volume().depth());
  auto& key_light = stage.key_light();
  glUniform4f(occlusion_detector_.key_light(), key_light.direction().x,
              key_light.direction().y, key_light.dispersion(),
              key_light.intensity());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene_buffer_.depth().id());
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, noise_texture_.id());
  glEnableVertexAttribArray(occlusion_detector_.position());
  DrawFullFrameQuad(occlusion_detector_.position());

  if (kFilterLightingBuffer) {
    scratch_texture_ =
        lighting_buffer_.SetColorTexture(std::move(scratch_texture_));
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(reconstruction_filter_.program().id());
    glUniform1i(reconstruction_filter_.illumination_map(), 0);
    glUniform2f(reconstruction_filter_.tap_stride(), 1.0f / size_.width(),
                0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scratch_texture_.id());
    glEnableVertexAttribArray(reconstruction_filter_.position());
    DrawFullFrameQuad(occlusion_detector_.position());

    scratch_texture_ =
        lighting_buffer_.SetColorTexture(std::move(scratch_texture_));
    glUniform2f(reconstruction_filter_.tap_stride(), 0.0f,
                1.0f / size_.height());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scratch_texture_.id());
    glEnableVertexAttribArray(reconstruction_filter_.position());
    DrawFullFrameQuad(occlusion_detector_.position());
  }
}

void Renderer::Blit(GLuint texture_id) {
  glUseProgram(blit_shader_.program().id());
  glUniform1i(blit_shader_.source(), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glEnableVertexAttribArray(blit_shader_.position());
  DrawFullFrameQuad(blit_shader_.position());
}

void Renderer::DrawFullFrameQuad(GLint position) {
  glVertexAttribPointer(position, 3, GL_FLOAT, GL_FALSE, 0, full_frame_.data());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT,
                 Quad::GetIndices());
}

}  // namespace escher
