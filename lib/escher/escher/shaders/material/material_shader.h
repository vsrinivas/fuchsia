// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <glm/glm.hpp>

#include "ftl/macros.h"
#include "escher/gl/unique_program.h"
#include "escher/scene/material.h"
#include "escher/scene/stage.h"
#include "escher/shaders/material/material_shader_descriptor.h"
#include "escher/shaders/material/modifier.h"

namespace escher {

// Shader for materials.
// Generally responsible for writing color and depth to the framebuffer.
class MaterialShader {
 public:
  ~MaterialShader();

  // Attributes
  GLint position() const { return position_; }
  GLint uv() const { return uv_; }

  // Prepares the program for use before drawing any number of objects.
  // Must be called whenever switching between shaders and before binding
  // a material.
  void BindProgram() const;

  // Binds the parameters of a material to the shader.
  void BindUniforms(const Stage& stage,
                    const Material& material,
                    const Modifier& modifier,
                    const mat4& matrix) const;

  bool NeedsUV() const;

 private:
  friend class MaterialShaderFactory;

  explicit MaterialShader(const MaterialShaderDescriptor& descriptor);
  bool Compile();
  std::string GeneratePrologue();

  const MaterialShaderDescriptor descriptor_;

  UniqueProgram program_;

  // Uniforms.
  GLint matrix_ = -1;
  GLint color_ = -1;
  GLint displacement_params0_ = -1;
  GLint displacement_params1_ = -1;

  // Attributes.
  GLint position_ = -1;
  GLint uv_ = -1;
  GLint texture_ = -1;
  GLint texture_matrix_ = -1;

  FTL_DISALLOW_COPY_AND_ASSIGN(MaterialShader);
};

}  // namespace escher
