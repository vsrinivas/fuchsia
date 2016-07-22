// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "ftl/macros.h"
#include "escher/gl/unique_buffer.h"
#include "escher/gl/unique_frame_buffer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/shaders/material/material_shader_factory.h"

namespace escher {

class ModelRenderer {
 public:
  ModelRenderer();
  ~ModelRenderer();

  bool Init();

  void DrawModel(const Stage& stage, const Model& model);

 private:
  class DrawContext {
   public:
    DrawContext(ModelRenderer& renderer,
                const Stage& stage,
                const mat4& matrix);
    ~DrawContext();

    void DrawObject(const Object& object);

   private:
    void DrawRect(const Object& object);
    void DrawCircle(const Object& object);
    void DrawMesh(const Object& object);

    void BindMaterial(
        const Material& material, const Modifier& modifier, const mat4& matrix);

    ModelRenderer& renderer_;
    const Stage& stage_;
    const mat4& matrix_;
    const MaterialShader* shader_ = nullptr;
  };

  MaterialShaderFactory material_shader_factory_;

  ftl::RefPtr<Mesh> circle_mesh_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelRenderer);
};

}  // namespace escher
