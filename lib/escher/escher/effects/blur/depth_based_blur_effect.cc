// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/effects/blur/depth_based_blur_effect.h"

#include "escher/geometry/quad.h"
#include "escher/gl/bindings.h"
#include "escher/rendering/canvas.h"

#include "ftl/logging.h"

#include <iostream>

namespace escher {
namespace {

constexpr char g_vertex_shader[] = R"GLSL(
  attribute vec3 a_position;
  varying vec2 fragment_uv;

  void main() {
    gl_Position = vec4(a_position.xyz, 1.0);
    fragment_uv = gl_Position.xy * 0.5 + 0.5;
  }
)GLSL";

constexpr char g_fragment_shader[] = R"GLSL(
  precision mediump float;
  uniform sampler2D u_color;
  uniform sampler2D u_depth;
  uniform vec2 u_tap;
  varying vec2 fragment_uv;
  uniform vec2 u_height_converter;
  uniform float u_blur_plane_height;

  // Compute the height above the stage (in 'Material Design' coordinates)
  // using the specified depth-buffer value (normalized from 0 to 1).
  float depthToHeight(float depth) {
    return u_height_converter.x - u_height_converter.y * depth;
  }

  void main() {
    // TODO(jjosh): array initialization syntax isn't available in GLSL 1.00
    float gaussian[6];
    gaussian[0] = 0.382925;
    gaussian[1] = 0.24173;
    gaussian[2] = 0.060598;
    gaussian[3] = 0.005977;
    gaussian[4] = 0.000229;
    gaussian[5] = 0.000003;

    // Height of the fragment above the Material Design 'stage'.
    // TODO(jjosh): Elaborate, referencing the MD spec.
    //              Also, see material_stage.cc
    float fragment_height = depthToHeight(texture2D(u_depth, fragment_uv).r);

    if (fragment_height > u_blur_plane_height) {
      // Fragment is above the blur plane, so don't blur it.
      gl_FragColor = vec4(texture2D(u_color, fragment_uv).rgb, 1.0);
    } else {
      // Depth of the fragment below the blur plane.  The blur radius is
      // proportional to this depth.
      float blur_depth = u_blur_plane_height - fragment_height;
      // TODO(jjosh): Add parameter to scale the impact of the blur_depth upon
      // the blur radius.  I would do this immediately, except that the blur
      // radius is already constrained by the hardcoded Gaussian kernel above.
      vec2 scaled_tap = u_tap * blur_depth * 0.1;

      vec3 accumulator = gaussian[0] * texture2D(u_color, fragment_uv).rgb;
      for (int i = 1; i < 6; ++i) {
        vec2 tap = float(i) * scaled_tap;
        accumulator += gaussian[i] * texture2D(u_color, fragment_uv + tap).rgb;
        accumulator += gaussian[i] * texture2D(u_color, fragment_uv - tap).rgb;
      }

      gl_FragColor = vec4(accumulator, 1.0);
    }
  }
)GLSL";

}  // namespace


DepthBasedBlurEffect::DepthBasedBlurEffect() {}

DepthBasedBlurEffect::~DepthBasedBlurEffect() {}

bool DepthBasedBlurEffect::Init(TextureCache* texture_cache) {
  texture_cache_ = texture_cache;
  program_ = MakeUniqueProgram(g_vertex_shader, g_fragment_shader);
  if (!program_)
    return false;
  position_ = glGetAttribLocation(program_.id(), "a_position");
  FTL_DCHECK(position_ != -1);
  color_ = glGetUniformLocation(program_.id(), "u_color");
  FTL_DCHECK(color_ != -1);
  depth_ = glGetUniformLocation(program_.id(), "u_depth");
  FTL_DCHECK(depth_ != -1);
  tap_ = glGetUniformLocation(program_.id(), "u_tap");
  FTL_DCHECK(tap_ != -1);
  height_converter_ = glGetUniformLocation(program_.id(), "u_height_converter");
  FTL_DCHECK(height_converter_ != -1);
  blur_plane_height_ =
      glGetUniformLocation(program_.id(), "u_blur_plane_height");
  FTL_DCHECK(blur_plane_height_ != -1);
  frame_buffer_ = FrameBuffer::Make();
  glGenBuffers(1, &vertex_buffer_);
  FTL_DCHECK(vertex_buffer_ != 0);
  glGenBuffers(1, &index_buffer_);
  FTL_DCHECK(index_buffer_ != 0);

  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  Quad quad(Quad::CreateFillClipSpace(0.f));
  glBufferData(GL_ARRAY_BUFFER,
      12 * sizeof(GLfloat), quad.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
      6 * sizeof(GLushort), Quad::GetIndices(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  return true;
}

void DepthBasedBlurEffect::Draw(const Stage& stage,
                                const Texture& color,
                                const Texture& depth,
                                float blur_plane_height,
                                GLuint framebuffer_id) {
  glPushGroupMarkerEXT(17, "BlurEffect::Draw");

  auto& size = stage.physical_size();
  auto& volume = stage.viewing_volume();

  frame_buffer_.Bind();
  frame_buffer_.SetColor(texture_cache_->GetColorTexture(size));
  glClear(GL_COLOR_BUFFER_BIT);

  // The same VBOs and texture settings are used for both the vertical and
  // horizontal blur passes.
  glUseProgram(program_.id());
  // Depth texture.
  glUniform1i(depth_, 1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, depth.id());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // Color texture.
  glUniform1i(color_, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, color.id());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // Index and vertex buffers.
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glEnableVertexAttribArray(position_);
  glVertexAttribPointer(position_, 3, GL_FLOAT, GL_FALSE, 0, 0);
  FTL_DCHECK(glGetError() == GL_NO_ERROR);
  // Allow conversion of depth values into stage height values.
  glUniform2f(height_converter_, volume.near(), volume.near() - volume.far());
  // Blur parameters.
  glUniform1f(blur_plane_height_, blur_plane_height);

  // Vertical blur.
  glUniform2f(tap_, 0.f, 2.f / size.height());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT, 0);

  // Horizontal blur.
  Texture blurred_color(frame_buffer_.TakeColor());
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_id);
  glClear(GL_COLOR_BUFFER_BIT);
  glUniform2f(tap_, 2.f / size.width(), 0.f);
  glBindTexture(GL_TEXTURE_2D, blurred_color.id());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT, 0);

  // Clean up.
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glUseProgram(0);
  texture_cache_->PutTexture(std::move(blurred_color));
  FTL_DCHECK(glGetError() == GL_NO_ERROR);
  glPopGroupMarkerEXT();
}

}  // namespace escher
