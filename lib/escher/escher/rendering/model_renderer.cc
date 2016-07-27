// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/rendering/model_renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

#include "escher/geometry/quad.h"
#include "escher/geometry/tessellation.h"
#include "escher/gl/gles2/bindings.h"

namespace escher {

ModelRenderer::ModelRenderer() {}

ModelRenderer::~ModelRenderer() {}

bool ModelRenderer::Init() {
  circle_mesh_ = ftl::MakeRefCounted<Mesh>(TessellateCircle(3, vec2(0.f), 1.f));
  return true;
}

void ModelRenderer::DrawModel(const Stage& stage, const Model& model) {
  glPushGroupMarkerEXT(25, "ModelRenderer::DrawModel");
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);

  // TODO(jeffbrown): Currently we are drawing the scene back to front.
  // Optimize the renderer to draw opaque content front to back (use GL_LESS)
  // then draw translucent content back to front (use GL_LEQUAL).  We may
  // also need to use multiple rendering passes for certain blending effects.
  glDepthFunc(GL_LEQUAL);

  mat4 matrix = stage.viewing_volume().GetProjectionMatrix();
  DrawContext context(*this, stage, matrix);

  for (const auto& object : model.objects())
    context.DrawObject(object);

  glDepthFunc(GL_LESS);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glPopGroupMarkerEXT();
}

ModelRenderer::DrawContext::DrawContext(ModelRenderer& renderer,
                                        const Stage& stage,
                                        const mat4& matrix)
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
    case Shape::Type::kMesh:
      DrawMesh(object);
      break;
  }
}

void ModelRenderer::DrawContext::DrawRect(const Object& object) {
  Modifier modifier;
  const Material& material = *object.material();
  BindMaterial(material, modifier, matrix_);

  Quad quad = Quad::CreateFromRect(object.shape().position(),
                                   object.shape().size(), object.shape().z());
  glVertexAttribPointer(shader_->position(), 3, GL_FLOAT, GL_FALSE, 0,
                        quad.data());

  if (material.has_texture()) {
    constexpr GLfloat uv[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    glVertexAttribPointer(shader_->uv(), 2, GL_FLOAT, GL_FALSE, 0, uv);
  }

  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT,
                 Quad::GetIndices());
}

void ModelRenderer::DrawContext::DrawCircle(const Object& object) {
  Modifier modifier;
  const Shape& shape = object.shape();
  vec3 translation(shape.position() - vec2(shape.radius()), shape.z());
  BindMaterial(
      *object.material(),
      modifier,
      glm::scale(
          glm::translate(matrix_, translation),
          vec3(shape.radius(), shape.radius(), 1.f)));

  auto& mesh = *renderer_.circle_mesh_.get();
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices().id());

  glVertexAttribPointer(shader_->position(), 3, GL_FLOAT, GL_FALSE, 0, 0);
  if (shader_->NeedsUV()) {
    FTL_DCHECK(mesh.has_uv());
    glVertexAttribPointer(
        shader_->uv(), 2, GL_FLOAT, GL_FALSE, 0, mesh.uv_offset());
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices().id());
  glDrawElements(
      GL_TRIANGLES, mesh.num_indices(), GL_UNSIGNED_SHORT, 0);

  // TODO(jjosh): this can be removed once we source all data from VBOs.
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void ModelRenderer::DrawContext::DrawMesh(const Object& object) {
  Modifier modifier;
  const Material& material = *object.material();
  const Shape& shape = object.shape();
  BindMaterial(
      material,
      modifier,
      glm::translate(matrix_, vec3(shape.position(), shape.z())));

  const Mesh& mesh = shape.mesh();
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices().id());

  glVertexAttribPointer(shader_->position(), 3, GL_FLOAT, GL_FALSE, 0, 0);
  if (shader_->NeedsUV()) {
    FTL_DCHECK(mesh.has_uv());
    glVertexAttribPointer(
        shader_->uv(), 2, GL_FLOAT, GL_FALSE, 0, mesh.uv_offset());
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices().id());
  glDrawElements(GL_TRIANGLES, mesh.num_indices(), GL_UNSIGNED_SHORT, 0);

  // TODO(jjosh): this can be removed once we source all data from VBOs.
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void ModelRenderer::DrawContext::BindMaterial(
    const Material& material, const Modifier& modifier, const mat4& matrix) {
  const MaterialShader* shader =
      renderer_.material_shader_factory_.GetShader(material, modifier);
  if (!shader) {
    std::cerr << "Failed to compile material shader." << std::endl;
    exit(1);
  }

  if (shader != shader_) {
    shader->BindProgram();
    shader_ = shader;
  }
  shader->BindUniforms(stage_, material, modifier, matrix);
}

}  // namespace escher
