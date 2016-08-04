// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/effects/lighting/lighting_effect.h"

#include <iostream>

#include "escher/gl/gles2/bindings.h"
#include "escher/rendering/canvas.h"

#include "ftl/logging.h"

namespace escher {
namespace {

constexpr bool kFilterLightingBuffer = true;

}  // namespace

LightingEffect::LightingEffect() {}

LightingEffect::~LightingEffect() {}

bool LightingEffect::Init(TextureCache* texture_cache, bool use_mipmap) {
  texture_cache_ = texture_cache;
  if (!shader_.Compile() || !blur_.Compile() ||
      !occlusion_detector_.Compile(texture_cache))
    return false;
  full_frame_ = Quad::CreateFillClipSpace(0.0f);
  return true;
}

void LightingEffect::Apply(
    const Stage& stage, Context* context,
    const Texture& unlit_color, const Texture& unlit_depth,
    const Texture& illumination_out, const Texture& lit_color) {
  GenerateIllumination(
      stage, context, unlit_color, unlit_depth, illumination_out);
  if (kFilterLightingBuffer) {
    FilterIllumination(stage, context, illumination_out);
  }
  ApplyIllumination(
      context, unlit_color, illumination_out, lit_color);
}

void LightingEffect::GenerateIllumination(
    const Stage& stage, Context* context,
    const Texture& unlit_color, const Texture& unlit_depth,
    const Texture& illumination_out) {
  RenderPassSpec pass_spec;
  pass_spec.color[0].texture = illumination_out;

  context->BeginRenderPass(pass_spec, "LightingEffect::PrepareIllumination");

  glUseProgram(occlusion_detector_.program().id());
  glUniform1i(occlusion_detector_.depth_map(), 0);
  glUniform1i(occlusion_detector_.noise(), 1);
  auto& viewing_volume = stage.viewing_volume();
  glUniform3f(occlusion_detector_.viewing_volume(), viewing_volume.width(),
              viewing_volume.height(), viewing_volume.depth());
  auto& key_light = stage.key_light();
  glUniform4f(occlusion_detector_.key_light(), key_light.direction().x,
              key_light.direction().y, key_light.dispersion(),
              key_light.intensity());

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, unlit_depth.id());

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, occlusion_detector_.noise_texture().id());

  glEnableVertexAttribArray(occlusion_detector_.position());
  DrawQuad(occlusion_detector_.position(), full_frame_);

  context->EndRenderPass();
}

void LightingEffect::FilterIllumination(
    const Stage& stage, Context* context,
    const Texture& illumination_out) {
  RenderPassSpec pass_spec;
  pass_spec.color[0].texture = illumination_out;

  auto& size = stage.physical_size();

  Texture half_blurred = texture_cache_->GetColorTexture(size);
  pass_spec.color[0].texture = half_blurred;
  context->BeginRenderPass(pass_spec, "LightingEffect::FilterIllumination 1");

  glUseProgram(blur_.program().id());
  glUniform1i(blur_.illumination(), 0);
  glUniform2f(blur_.stride(), 1.0f / size.width(), 0.0f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, illumination_out.id());

  glEnableVertexAttribArray(blur_.position());
  DrawQuad(blur_.position(), full_frame_);

  context->EndRenderPass();

  pass_spec.color[0].texture = illumination_out;
  context->BeginRenderPass(pass_spec, "LightingEffect::FilterIllumination 2");

  glUniform2f(blur_.stride(), 0.0f, 1.0f / size.height());

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, half_blurred.id());

  glEnableVertexAttribArray(blur_.position());
  DrawQuad(blur_.position(), full_frame_);

  context->EndRenderPass();
}

void LightingEffect::ApplyIllumination(Context* context,
                                       const Texture& unlit_color,
                                       const Texture& illumination_out,
                                       const Texture& lit_color) {
  RenderPassSpec pass_spec;
  pass_spec.color[0].texture = lit_color;
  context->BeginRenderPass(pass_spec, "LightingEffect::ApplyIllumination");

  glUseProgram(shader_.program().id());
  glUniform1i(shader_.color(), 0);
  glUniform1i(shader_.illumination(), 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, unlit_color.id());

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, illumination_out.id());

  glEnableVertexAttribArray(shader_.position());
  DrawQuad(shader_.position(), full_frame_);

  context->EndRenderPass();
}

}  // namespace escher
