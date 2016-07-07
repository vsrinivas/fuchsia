// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/effects/lighting/occlusion_detector.h"

#include "ftl/logging.h"
#include "escher/effects/lighting/noise_texture.h"

namespace escher {
namespace {

// Must match fragment shader and RADIUS in IlluminationReconstructionFilter.
constexpr int kNoiseSize = 5;

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

  // Depth information about the scene.
  //
  // The shader assumes that the depth information in the r channel.
  uniform sampler2D u_depth_map;

  // A random texture of size kNoiseSize.
  uniform sampler2D u_noise;

  // The size of the viewing volume in (width, height, depth).
  uniform vec3 u_viewing_volume;

  // A description of the directional key light:
  //
  //  * theta, phi: The direction from which the light is received. The first
  //    coordinate is theta (the the azimuthal angle, in radians) and the second
  //    coordinate is phi (the polar angle, in radians).
  //  * dispersion: The angular variance in the light, in radians.
  //  * intensity: The amount of light emitted.
  uniform vec4 u_key_light;

  varying vec2 fragment_uv;

  const float kPi = 3.14159265359;

  // Must match header.
  const int kNoiseSize = 5;

  // The numer of screen-space samples to use in the computation.
  const int kTapCount = 8;

  // These should be relatively primary to each other and to kTapCount;
  const vec2 kSpirals = vec2(7.0, 5.0);

  // TODO(abarth): Make the shader less sensitive to this parameter.
  const float kSampleRadius = 16.0;  // screen pixels.

  float sampleKeyIllumination(vec2 fragment_uv,
                              float fragment_z,
                              float alpha,
                              vec2 seed) {
    float key_light_dispersion = u_key_light.z;
    vec2 key_light0 = u_key_light.xy - key_light_dispersion / 2.0;
    float theta = key_light0.x + fract(seed.x + alpha * kSpirals.x) * key_light_dispersion;
    float radius = alpha * kSampleRadius;

    vec2 tap_delta_uv = radius * vec2(cos(theta), sin(theta)) / u_viewing_volume.xy;
    float tap_depth_uv = texture2D(u_depth_map, fragment_uv + tap_delta_uv).r;
    float tap_z = tap_depth_uv * -u_viewing_volume.z;

    return 1.0 - max(0.0, (tap_z - fragment_z) / radius);
  }

  float sampleFillIllumination(vec2 fragment_uv,
                               float fragment_z,
                               float alpha,
                               vec2 seed) {
    float theta = 2.0 * kPi * (seed.x + alpha * kSpirals.x);
    float radius = alpha * kSampleRadius;

    vec2 tap_delta_uv = radius * vec2(cos(theta), sin(theta)) / u_viewing_volume.xy;
    float tap_depth_uv = texture2D(u_depth_map, fragment_uv + tap_delta_uv).r;
    float tap_z = tap_depth_uv * -u_viewing_volume.z;

    return 1.0 - max(0.0, (tap_z - fragment_z) / radius);
  }

  void main() {
    vec2 seed = texture2D(u_noise, fract(gl_FragCoord.xy / float(kNoiseSize))).rg;

    float fragment_depth_uv = texture2D(u_depth_map, fragment_uv).r;
    float fragment_z = fragment_depth_uv * -u_viewing_volume.z;

    float key_light_intensity = u_key_light.w;
    float fill_light_intensity = 1.0 - key_light_intensity;

    float L = 0.0;
    for (int i = 0; i < kTapCount; ++i) {
      float alpha = (float(i) + 0.5) / float(kTapCount);
      L += key_light_intensity * sampleKeyIllumination(fragment_uv, fragment_z, alpha, seed);
      L += fill_light_intensity * sampleFillIllumination(fragment_uv, fragment_z, alpha, seed);
    }
    L = clamp(L / float(kTapCount), 0.0, 1.0);
    gl_FragColor = vec4(L, 0.0, fragment_depth_uv, 1.0);
  }
)GLSL";

}  // namespace

OcclusionDetector::OcclusionDetector() {
}

OcclusionDetector::~OcclusionDetector() {
}

bool OcclusionDetector::Compile() {
  program_ = MakeUniqueProgram(g_vertex_shader, g_fragment_shader);
  if (!program_)
    return false;
  depth_map_ = glGetUniformLocation(program_.id(), "u_depth_map");
  FTL_DCHECK(depth_map_ != -1);
  noise_ = glGetUniformLocation(program_.id(), "u_noise");
  FTL_DCHECK(noise_ != -1);
  viewing_volume_ = glGetUniformLocation(program_.id(), "u_viewing_volume");
  FTL_DCHECK(viewing_volume_ != -1);
  key_light_ = glGetUniformLocation(program_.id(), "u_key_light");
  FTL_DCHECK(key_light_ != -1);

  noise_texture_ = MakeNoiseTexture(SizeI(kNoiseSize, kNoiseSize));
  return true;
}

}  // namespace escher
