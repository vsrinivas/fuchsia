// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/lighting/illumination_shader.h"

namespace escher {
namespace {

constexpr char g_vertex_shader[] = R"GLSL(
  attribute vec3 a_position;
  varying vec2 fragment_uv;

  void main() {
    gl_Position = vec4(a_position, 1.0);
    fragment_uv = gl_Position.xy * 0.5 + 0.5;
  }
)GLSL";

constexpr char g_fragment_shader[] = R"GLSL(
  precision mediump float;
  uniform sampler2D u_scene;
  uniform sampler2D u_lighting;
  varying vec2 fragment_uv;

  void main() {
    vec3 scene = texture2D(u_scene, fragment_uv.xy).rgb;
    float illumination = texture2D(u_lighting, fragment_uv.xy).r;
    gl_FragColor = vec4(illumination * scene, 1.0);
  }
)GLSL";

}  // namespace

IlluminationShader::IlluminationShader() {}

IlluminationShader::~IlluminationShader() {}

bool IlluminationShader::Compile() {
  program_ = MakeUniqueProgram(g_vertex_shader, g_fragment_shader);
  if (!program_)
    return false;
  scene_ = glGetUniformLocation(program_.id(), "u_scene");
  ESCHER_DCHECK(scene_ != -1);
  lighting_ = glGetUniformLocation(program_.id(), "u_lighting");
  ESCHER_DCHECK(lighting_ != -1);
  return true;
}

}  // namespace escher
