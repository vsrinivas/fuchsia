// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/material/material_shader.h"

#include <math.h>

#include "ftl/logging.h"
#include "escher/gl/unique_shader.h"
#include "escher/shaders/glsl_generator.h"

namespace escher {
namespace {

constexpr char g_decls[] = R"GLSL(
  #define BINDING_NONE 0
  #define BINDING_CONSTANT 1
  #define MASK_NONE 0
  #define MASK_CIRCULAR 1
  #define DISPLACEMENT_NONE 0
  #define DISPLACEMENT_WAVE 1

  #define HAS_MASK (MASK != MASK_NONE)
  #define HAS_DISPLACEMENT (DISPLACEMENT != DISPLACEMENT_NONE)

  #define NEEDS_UV (HAS_MASK || HAS_DISPLACEMENT || HAS_TEXTURE)
  #define NEEDS_DEPTH HAS_DISPLACEMENT
)GLSL";

constexpr char g_vertex_shader[] = R"GLSL(
  attribute vec3 a_position;
  uniform mat4 u_matrix;

#if NEEDS_UV
  attribute vec2 a_uv;
  varying vec2 v_uv;
#endif

  void main() {
    gl_Position = u_matrix * vec4(a_position, 1.0);
#if NEEDS_UV
    v_uv = a_uv;
#endif
  }
)GLSL";

constexpr char g_fragment_shader[] = R"GLSL(
#if NEEDS_DEPTH
  #extension GL_EXT_frag_depth : require
#endif

  precision mediump float;

  const float kPi = 3.14159265359;

#if NEEDS_UV
  varying vec2 v_uv;
#endif

#if HAS_TEXTURE
  uniform sampler2D u_texture;
  // TODO(jjosh): more efficiently done in vertex shader?
  uniform vec4 u_texture_matrix;
  vec4 color() {
    return texture2D(u_texture, mat2(u_texture_matrix) * v_uv);
  }
#elif COLOR_BINDING == BINDING_NONE
  vec4 color() {
    return vec4(0.0, 0.0, 0.0, 1.0); // should alpha be 0?
  }
#elif COLOR_BINDING == BINDING_CONSTANT
  uniform vec4 u_color;
  vec4 color() {
    return u_color;
  }
#endif

#if MASK == MASK_CIRCULAR
  void applyMask() {
    vec2 r = 2.0 * v_uv - 1.0;
    if (dot(r, r) >= 1.0)
      discard; // TODO(jeffbrown): inefficient!
  }
#endif

#if DISPLACEMENT == DISPLACEMENT_WAVE
  uniform vec4 u_displacement_params0;
  uniform vec4 u_displacement_params1;

  // TODO(abarth): Compute this value analytically.
  const float kDisplacementShadow = 0.08;

  void applyDisplacement() {
    vec2 peak = u_displacement_params0.xy;
    vec2 unit_wavevector = u_displacement_params0.zw;
    float half_wavenumber = u_displacement_params1.x;
    float amplitude = u_displacement_params1.y;
    float theta_min = u_displacement_params1.z;
    float theta_max = u_displacement_params1.w;

    float theta = clamp(dot(v_uv - peak, unit_wavevector) * half_wavenumber, theta_min, theta_max);

    // TODO(abarth): The shadow should vary with the projection of the
    // unit_wavevector onto the key light direction.
    float shadow = kDisplacementShadow * abs(sin(theta));
    gl_FragColor.rgb = (1.0 - shadow) * gl_FragColor.rgb;
    gl_FragDepthEXT = gl_FragCoord.z + amplitude * (1.0 + cos(theta));
  }
#endif

  void main() {
#if HAS_MASK
    applyMask();
#endif
    gl_FragColor = color();
#if HAS_DISPLACEMENT
    applyDisplacement();
#endif
  }
)GLSL";

void DefineBindingSymbol(GLSLGenerator& generator,
                         const std::string& symbol,
                         BindingType binding_type) {
  switch (binding_type) {
    case BindingType::kNone:
      generator.DefineSymbol(symbol, "BINDING_NONE");
      break;
    case BindingType::kConstant:
      generator.DefineSymbol(symbol, "BINDING_CONSTANT");
      break;
  }
}

void DefineMaskSymbol(GLSLGenerator& generator, Modifier::Mask mask) {
  switch (mask) {
    case Modifier::Mask::kNone:
      generator.DefineSymbol("MASK", "MASK_NONE");
      break;
    case Modifier::Mask::kCircular:
      generator.DefineSymbol("MASK", "MASK_CIRCULAR");
      break;
  }
}

void DefineDisplacementSymbol(GLSLGenerator& generator,
                              Displacement::Type displacement) {
  switch (displacement) {
    case Displacement::Type::kNone:
      generator.DefineSymbol("DISPLACEMENT", "DISPLACEMENT_NONE");
      break;
    case Displacement::Type::kWave:
      generator.DefineSymbol("DISPLACEMENT", "DISPLACEMENT_WAVE");
      break;
  }
}

void DefineTextureSymbol(GLSLGenerator& generator, bool has_texture) {
  generator.DefineSymbol("HAS_TEXTURE", has_texture ? "1" : "0");
}

}  // namespace

MaterialShader::MaterialShader(const MaterialShaderSpec& spec)
    : spec_(spec) {}

MaterialShader::~MaterialShader() {}

void MaterialShader::BindProgram() const {
  glUseProgram(program_.id());
  glEnableVertexAttribArray(position_);
  if (NeedsUV()) glEnableVertexAttribArray(uv_);
}

void MaterialShader::BindUniforms(const Stage& stage,
                                  const Material& material,
                                  const Modifier& modifier,
                                  const mat4& matrix) const {
  glUniformMatrix4fv(matrix_, 1, GL_FALSE, &matrix[0][0]);
  if (spec_.color_binding_type == BindingType::kConstant) {
    const vec4& color = material.color().constant_value();
    glUniform4fv(color_, 1, &color[0]);
  }

  FTL_DCHECK(material.has_texture() == spec_.has_texture);
  if (material.has_texture()) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, material.texture());
    const mat2& texture_matrix = material.texture_matrix().constant_value();
    glUniform4fv(texture_matrix_, 1, &texture_matrix[0][0]);
  }

  if (material.displacement().type() == Displacement::Type::kWave) {
    auto& displacement = material.displacement();
    vec2 wavevector = displacement.end() - displacement.start();
    vec2 peak = displacement.start() + wavevector / 2.0f;
    float wavelength = glm::length(wavevector);
    vec2 unit_wavevector = wavevector / wavelength;
    float half_wavenumber = M_PI / wavelength;
    float amplitude =
        0.5f * displacement.max() / -stage.viewing_volume().depth();

    glUniform4f(displacement_params0_, peak.x, peak.y, unit_wavevector.x,
                unit_wavevector.y);
    glUniform4f(displacement_params1_, half_wavenumber, amplitude,
                displacement.theta_min(), displacement.theta_max());
  }
}

bool MaterialShader::NeedsUV() const {
  return spec_.displacement != Displacement::Type::kNone ||
         spec_.mask != Modifier::Mask::kNone ||
         spec_.has_texture;
}

bool MaterialShader::Compile() {
  std::string prologue = GeneratePrologue();
  std::vector<std::string> vertex_shader_sources{g_decls, prologue,
                                                 g_vertex_shader};
  std::vector<std::string> fragment_shader_sources{g_decls, prologue,
                                                   g_fragment_shader};
  UniqueShader vertex_shader =
      MakeUniqueShader(GL_VERTEX_SHADER, vertex_shader_sources);
  UniqueShader fragment_shader =
      MakeUniqueShader(GL_FRAGMENT_SHADER, fragment_shader_sources);
  if (!vertex_shader || !fragment_shader)
    return false;

  program_ =
      MakeUniqueProgram(std::move(vertex_shader), std::move(fragment_shader));
  if (!program_)
    return false;

  matrix_ = glGetUniformLocation(program_.id(), "u_matrix");
  FTL_DCHECK(matrix_ != -1);

  if (spec_.color_binding_type == BindingType::kConstant) {
    color_ = glGetUniformLocation(program_.id(), "u_color");
    FTL_DCHECK(color_ != -1);
  }

  if (spec_.displacement != Displacement::Type::kNone) {
    displacement_params0_ =
        glGetUniformLocation(program_.id(), "u_displacement_params0");
    FTL_DCHECK(displacement_params0_ != -1);
    displacement_params1_ =
        glGetUniformLocation(program_.id(), "u_displacement_params1");
    FTL_DCHECK(displacement_params1_ != -1);
  }

  if (spec_.has_texture) {
    texture_ = glGetUniformLocation(program_.id(), "u_texture");
    texture_matrix_ = glGetUniformLocation(program_.id(), "u_texture_matrix");
    FTL_DCHECK(texture_ != -1);
    FTL_DCHECK(texture_matrix_ != -1);
  }

  position_ = glGetAttribLocation(program_.id(), "a_position");
  FTL_DCHECK(position_ != -1);

  if (NeedsUV()) {
    uv_ = glGetAttribLocation(program_.id(), "a_uv");
    FTL_DCHECK(uv_ != -1);
  }
  return true;
}

std::string MaterialShader::GeneratePrologue() {
  GLSLGenerator generator;
  DefineBindingSymbol(generator, "COLOR_BINDING",
                      spec_.color_binding_type);
  DefineDisplacementSymbol(generator, spec_.displacement);
  DefineMaskSymbol(generator, spec_.mask);
  DefineTextureSymbol(generator, spec_.has_texture);

  return generator.GenerateCode();
}

}  // namespace escher
