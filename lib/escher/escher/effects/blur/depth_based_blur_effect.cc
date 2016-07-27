// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/effects/blur/depth_based_blur_effect.h"

#include "escher/geometry/quad.h"
#include "escher/gl/gles2/bindings.h"
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
  #extension GL_EXT_shader_texture_lod : require

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

  vec4 weightedSample(vec2 sample_uv, float weight, float fragment_height) {
    // TODO(jjosh): this value is ad-hoc.
    const float cutoff = 0.05;
    // TODO(jjosh): since (fragment_height + cutoff) doesn't change between
    // samples, it might be more efficient to compute it once and pass it in.
    float sample_height = depthToHeight(texture2D(u_depth, sample_uv).r);
    if (sample_height - fragment_height > cutoff) {
      // The sample is significantly higher (closer to the camera) than the
      // fragment.  Ideally, we would sample the pixel 'under' the current one,
      // but since we can't we just ignore it.
      // TODO(jjosh): investigate whether there are approaches that can address
      // or mitigate this issue (depth peeling?).
      return vec4(0.0, 0.0, 0.0, 0.0);
    }

    float lod = max(0.0, log2((u_blur_plane_height - sample_height) * 0.3));
    vec4 result = vec4(texture2DLodEXT(u_color, sample_uv, lod).rgb, 1.0);
    return result * weight;
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

    // TODO(jjosh): remove when we are happy with the results.  Until then,
    // this is useful for visualizing the radius of the blur.
    if (false) {
      gaussian[0] = 0.0;
      gaussian[1] = 1.0;
      gaussian[2] = 0.0;
      gaussian[3] = 0.0;
      gaussian[4] = 0.0;
      gaussian[5] = 1.0;
    }

    // Height of the fragment above the Material Design 'stage'.
    // TODO(jjosh): Elaborate, referencing the MD spec.
    //              Also, see material_stage.cc
    float fragment_depth = texture2D(u_depth, fragment_uv).r;
    float fragment_height = depthToHeight(fragment_depth);

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
      vec2 scaled_tap = u_tap * blur_depth * 0.4;

      // TODO(jjosh): calling weightedSample() here incurs a redundant lookup of
      // the fragment depth; this is currently worthwhile because it eases
      // experimentation.
      vec4 accumulator = weightedSample(fragment_uv, gaussian[0], fragment_height);

      for (int i = 1; i < 6; ++i) {
        vec2 tap = float(i) * scaled_tap;
        vec2 sample_uv = fragment_uv + tap;
        accumulator += weightedSample(sample_uv, gaussian[i], fragment_height);
        sample_uv = fragment_uv - tap;
        accumulator += weightedSample(sample_uv, gaussian[i], fragment_height);
      }

      gl_FragColor = vec4(accumulator.rgb / accumulator.a, 1.0);
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

  vertex_buffer_ = MakeUniqueBuffer();
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_.id());
  Quad quad(Quad::CreateFillClipSpace(0.f));
  glBufferData(GL_ARRAY_BUFFER,
      12 * sizeof(GLfloat), quad.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  index_buffer_ = MakeUniqueBuffer();
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_.id());
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
  auto& offset = stage.viewport_offset();
  auto& volume = stage.viewing_volume();

  frame_buffer_.Bind();
  frame_buffer_.SetColor(texture_cache_->GetMipmappedColorTexture(size));
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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // Index and vertex buffers.
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_.id());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_.id());
  glEnableVertexAttribArray(position_);
  glVertexAttribPointer(position_, 3, GL_FLOAT, GL_FALSE, 0, 0);
  FTL_DCHECK(glGetError() == GL_NO_ERROR);
  // Allow conversion of depth values into stage height values.
  glUniform2f(height_converter_, volume.near(), volume.near() - volume.far());
  // Blur parameters.
  glUniform1f(blur_plane_height_, blur_plane_height);

  // Vertical blur.
  // TODO(jjosh): This is a one-pixel step in the source texture.  However, the
  // blur radius should be in terms of the stage viewing volume (i.e. should not
  // depend on whether the screen is "retina" or not).  Same applies to the
  // horizontal blur below.
  glUniform2f(tap_, 0.f, 1.f / size.height());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT, 0);

  // Horizontal blur.
  Texture blurred_color(frame_buffer_.TakeColor());
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_id);
  glViewport(offset.width(), offset.height(), size.width(), size.height());
  glClear(GL_COLOR_BUFFER_BIT);
  glUniform2f(tap_, 1.f / size.width(), 0.f);
  glBindTexture(GL_TEXTURE_2D, blurred_color.id());

  glPushGroupMarkerEXT(37, "DepthBasedBlurEffect::GenerateMipmap");
  glGenerateMipmap(GL_TEXTURE_2D);
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    std::cerr << "DepthBasedBlurEffect::GenerateMipmap() failed: "
              << gl_error << std::endl;
    FTL_DCHECK(false);
  }
  glPopGroupMarkerEXT();

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
