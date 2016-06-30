// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "escher/base/macros.h"
#include "escher/gl/unique_frame_buffer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/shaders/material/material_shader_factory.h"

namespace escher {

// Draws the contents of a model.
class ModelRenderer {
 public:
  ModelRenderer();
  ~ModelRenderer();

  // Draws the content of the model to the specified frame buffer which
  // must have color and depth buffers attached.
  void DrawModel(const Stage& stage,
                 const Model& model,
                 const glm::mat4& matrix,
                 const UniqueFrameBuffer& frame_buffer);

 private:
  class DrawContext {
   public:
    DrawContext(ModelRenderer& renderer,
                const Stage& stage,
                const glm::mat4& matrix);
    ~DrawContext();

    void DrawObject(const Object& object);

   private:
    void DrawRect(const Object& object);
    void DrawCircle(const Object& object);

    void BindMaterial(const Material& material, const Modifier& modifier);
    void UseMaterialShader(const MaterialShader* shader);

    ModelRenderer& renderer_;
    const Stage& stage_;
    const glm::mat4& matrix_;
    const MaterialShader* shader_ = nullptr;
  };

  MaterialShaderFactory material_shader_factory_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(ModelRenderer);
};

}  // namespace escher
