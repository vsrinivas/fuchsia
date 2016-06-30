// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/rendering/model_renderer.h"

#include <iostream>

#include "escher/geometry/quad.h"

namespace escher {

ModelRenderer::ModelRenderer() {}

ModelRenderer::~ModelRenderer() {}

void ModelRenderer::DrawModel(const Stage& stage,
                              const Model& model,
                              const glm::mat4& matrix,
                              const UniqueFrameBuffer& frame_buffer) {
  glBindFramebuffer(GL_FRAMEBUFFER, frame_buffer.id());
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);

  // TODO(jeffbrown): Currently we are drawing the scene back to front.
  // Optimize the renderer to draw opaque content front to back (use GL_LESS)
  // then draw translucent content back to front (use GL_LEQUAL).  We may
  // also need to use multiple rendering passes for certain blending effects.
  glDepthFunc(GL_LEQUAL);

  DrawContext context(*this, stage, matrix);

  for (const auto& object : model.objects())
    context.DrawObject(object);

  glDepthFunc(GL_LESS);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

ModelRenderer::DrawContext::DrawContext(ModelRenderer& renderer,
                                        const Stage& stage,
                                        const glm::mat4& matrix)
    : renderer_(renderer), stage_(stage), matrix_(matrix) {}

ModelRenderer::DrawContext::~DrawContext() {}

void ModelRenderer::DrawContext::DrawObject(const Object& object) {
  switch (object.shape().type()) {
    case Shape::Type::kRect:
      DrawRect(object);
      break;
    case Shape::Type::kCircle:
      DrawCircle(object);
      break;
  }
}

void ModelRenderer::DrawContext::DrawRect(const Object& object) {
  Modifier modifier;
  BindMaterial(*object.material(), modifier);

  Quad quad = Quad::CreateFromRect(object.shape().position(),
                                   object.shape().size(), object.shape().z());
  glVertexAttribPointer(shader_->position(), 3, GL_FLOAT, GL_FALSE, 0,
                        quad.data());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT,
                 Quad::GetIndices());
}

void ModelRenderer::DrawContext::DrawCircle(const Object& object) {
  // TODO(jeffbrown): This whole approach to drawing circles is rather
  // inefficient and ugly but it is sufficient to exercise the pipeline.
  Modifier modifier;
  modifier.set_mask(Modifier::Mask::kCircular);
  BindMaterial(*object.material(), modifier);

  Quad quad = Quad::CreateFromRect(object.shape().position(),
                                   object.shape().size(), object.shape().z());
  glVertexAttribPointer(shader_->position(), 3, GL_FLOAT, GL_FALSE, 0,
                        quad.data());

  constexpr GLfloat uv[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  glVertexAttribPointer(shader_->uv(), 2, GL_FLOAT, GL_FALSE, 0, uv);

  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT,
                 Quad::GetIndices());
}

void ModelRenderer::DrawContext::BindMaterial(const Material& material,
                                              const Modifier& modifier) {
  const MaterialShader* shader =
      renderer_.material_shader_factory_.GetShader(material, modifier);
  if (!shader) {
    std::cerr << "Failed to compile material shader." << std::endl;
    exit(1);
  }

  UseMaterialShader(shader);
  shader->Bind(stage_, material, modifier);
}

void ModelRenderer::DrawContext::UseMaterialShader(
    const MaterialShader* shader) {
  ESCHER_DCHECK(shader);

  if (shader == shader_)
    return;

  shader->Use(matrix_);
  shader_ = shader;
}

}  // namespace escher
