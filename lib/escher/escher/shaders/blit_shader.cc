// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/blit_shader.h"

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
  uniform sampler2D u_source;
  varying vec2 fragment_uv;

  void main() {
    gl_FragColor = texture2D(u_source, fragment_uv);
  }
)GLSL";

}  // namespace

BlitShader::BlitShader() {}

BlitShader::~BlitShader() {}

bool BlitShader::Compile() {
  program_ = MakeUniqueProgram(g_vertex_shader, g_fragment_shader);
  if (!program_)
    return false;
  source_ = glGetUniformLocation(program_.id(), "u_source");
  ESCHER_DCHECK(source_ != -1);
  return true;
}

}  // namespace escher
