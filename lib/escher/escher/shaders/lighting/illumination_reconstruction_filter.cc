// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/lighting/illumination_reconstruction_filter.h"

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
  uniform sampler2D u_illumination_map;
  uniform vec2 u_tap_stride;
  varying vec2 fragment_uv;

  const float scene_depth = 26.0;

  #define RADIUS 4

  void main() {
    vec4 center_tap = texture2D(u_illumination_map, fragment_uv);
    float center_key = center_tap.z * scene_depth;

    float sum = center_tap.x;
    float total_weight = 1.0;

    for (int r = 1; r <= RADIUS; ++r) {
      vec4 left_tap = texture2D(u_illumination_map, fragment_uv + float(-r) * u_tap_stride);
      float left_tap_key = left_tap.z * scene_depth;
      float left_key_weight = max(0.0, 1.0 - abs(left_tap_key - center_key));

      vec4 right_tap = texture2D(u_illumination_map, fragment_uv + float(r) * u_tap_stride);
      float right_tap_key = right_tap.z * scene_depth;
      float right_key_weight = max(0.0, 1.0 - abs(right_tap_key - center_key));

      float position_weight = float(RADIUS - r + 1) / float(RADIUS + 1);
      float tap_weight = position_weight * left_key_weight * right_key_weight;

      sum += tap_weight * left_tap.x + tap_weight * right_tap.x;
      total_weight += 2.0 * tap_weight;
    }

    float illumination = sum / total_weight;
    gl_FragColor = vec4(illumination, 0.0, center_tap.z, 1.0);
  }
)GLSL";

}  // namespace

IlluminationReconstructionFilter::IlluminationReconstructionFilter() {}

IlluminationReconstructionFilter::~IlluminationReconstructionFilter() {}

bool IlluminationReconstructionFilter::Compile() {
  program_ = MakeUniqueProgram(g_vertex_shader, g_fragment_shader);
  if (!program_)
    return false;
  illumination_map_ = glGetUniformLocation(program_.id(), "u_illumination_map");
  ESCHER_DCHECK(illumination_map_ != -1);
  tap_stride_ = glGetUniformLocation(program_.id(), "u_tap_stride");
  ESCHER_DCHECK(tap_stride_ != -1);
  return true;
}

}  // namespace escher
