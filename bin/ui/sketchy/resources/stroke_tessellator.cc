// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_tessellator.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/texture.h"

namespace {

constexpr uint32_t kLocalSize = 32;

constexpr char kShaderCode[] = R"GLSL(

#version 450
#extension GL_ARB_separate_shader_objects : enable

struct Bezier2f {
  vec2 pts[4];
};

struct Bezier1f {
  float pts[4];
};

struct Vertex {
  vec2 pos;
  vec2 uv;
};

layout(local_size_x = 32) in;

layout(binding = 0) uniform StrokeInfo {
  uint segment_count;
  float half_width;
  uint base_vertex_index;
  float pixels_per_division;
  uint division_count;
  float total_length;
};

layout(std430, binding = 1) buffer ControlPoints {
  Bezier2f control_points[];
};

layout(std430, binding = 2) buffer ReParams {
  Bezier1f re_params[];
};

layout(std430, binding = 3) buffer DivisionCounts {
  uint division_counts[];
};

layout(std430, binding = 4) buffer CumulativeDivisionCounts {
  uint cumulative_division_counts[];
};

layout(std430, binding = 5) buffer Vertices {
  Vertex vertices[];
};

layout(std430, binding = 6) buffer Indices {
  uint indices[];
};

// TODO(MZ-269): Do binary search, along with other optimizations.
uint FindSegmentIndex(uint division_idx) {
  for (uint i = 1; i < segment_count; i++) {
    if (cumulative_division_counts[i] > division_idx) {
      return i - 1;
    }
  }
  return segment_count - 1;
}

void EvaluatePointAndNormal(in Bezier2f bezier2f, in float t,
                            out vec2 point, out vec2 normal) {
  vec2 tmp3[3];
  vec2 tmp2[2];
  float t_rest = 1 - t;
  tmp3[0] = bezier2f.pts[0] * t_rest + bezier2f.pts[1] * t;
  tmp3[1] = bezier2f.pts[1] * t_rest + bezier2f.pts[2] * t;
  tmp3[2] = bezier2f.pts[2] * t_rest + bezier2f.pts[3] * t;
  tmp2[0] = tmp3[0] * t_rest + tmp3[1] * t;
  tmp2[1] = tmp3[1] * t_rest + tmp3[2] * t;
  point = tmp2[0] * t_rest + tmp2[1] * t;
  vec2 tangent = normalize(tmp2[1] - tmp2[0]);
  normal = vec2(-tangent.y, tangent.x);
}

float ReParam(Bezier1f bezier1f, float t) {
  float tmp3[3];
  float tmp2[2];
  float t_rest = 1 - t;
  tmp3[0] = bezier1f.pts[0] * t_rest + bezier1f.pts[1] * t;
  tmp3[1] = bezier1f.pts[1] * t_rest + bezier1f.pts[2] * t;
  tmp3[2] = bezier1f.pts[2] * t_rest + bezier1f.pts[3] * t;
  tmp2[0] = tmp3[0] * t_rest + tmp3[1] * t;
  tmp2[1] = tmp3[1] * t_rest + tmp3[2] * t;
  return tmp2[0] * t_rest + tmp2[1] * t;
}

void main() {
  uint division_idx = gl_GlobalInvocationID.x;
  if (division_idx >= division_count) {
    return;
  }

  uint segment_idx = FindSegmentIndex(division_idx);
  float division_idx_in_segment =
      division_idx - cumulative_division_counts[segment_idx];
  float t_before_re_param =
      float(division_idx - cumulative_division_counts[segment_idx]) /
      division_counts[segment_idx];
  float t = ReParam(re_params[segment_idx], t_before_re_param);

  float progress = float(division_idx) / division_count;
  vec2 point, normal;
  EvaluatePointAndNormal(control_points[segment_idx], t, point, normal);
  uint vertex_idx = division_idx * 2;
  vertices[vertex_idx].pos = point + normal * half_width;
  vertices[vertex_idx].uv = vec2(progress, 0);
  vertices[vertex_idx + 1].pos = point - normal * half_width;
  vertices[vertex_idx + 1].uv = vec2(progress, 0);

  if (division_idx < division_count - 1) {
    uint index_idx = division_idx * 6;
    uint vertex_idx = base_vertex_index + division_idx * 2;
    indices[index_idx] = vertex_idx;
    indices[index_idx + 1] = vertex_idx + 1;
    indices[index_idx + 2] = vertex_idx + 3;
    indices[index_idx + 3] = vertex_idx;
    indices[index_idx + 4] = vertex_idx + 3;
    indices[index_idx + 5] = vertex_idx + 2;
  } else {
    // division_count is guaranteed to be > 0.
    uint index_idx = (division_count - 1) * 6;
    // There're no corresponding vertices, so drop the last division.
    indices[index_idx] = 0;
    indices[index_idx + 1] = 0;
    indices[index_idx + 2] = 0;
    indices[index_idx + 3] = 0;
    indices[index_idx + 4] = 0;
    indices[index_idx + 5] = 0;
  }
}

)GLSL";

}  // namespace

namespace sketchy_service {

StrokeTessellator::StrokeTessellator(escher::Escher* escher)
    : kernel_(escher, std::vector<vk::ImageLayout>{},
              std::vector<vk::DescriptorType>{
                  // Binding 0: |stroke_info_buffer|
                  vk::DescriptorType::eUniformBuffer,
                  // Binding 1: |control_points_buffer|
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 2: |re_params_buffer|
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 3: |division_counts_buffer|
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 4: |cumulative_division_counts_buffer|
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 5: output vertex buffer
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 6: output index buffer
                  vk::DescriptorType::eStorageBuffer},
              /* push_constants_size= */ 0, kShaderCode) {}

void StrokeTessellator::Dispatch(
    escher::BufferPtr stroke_info_buffer,
    escher::BufferPtr control_points_buffer,
    escher::BufferPtr re_params_buffer,
    escher::BufferPtr division_counts_buffer,
    escher::BufferPtr cumulative_division_counts_buffer,
    escher::BufferPtr vertex_buffer,
    escher::BufferPtr index_buffer,
    escher::impl::CommandBuffer* command,
    uint32_t division_count) {
  uint32_t group_count = (division_count + kLocalSize - 1) / kLocalSize;
  kernel_.Dispatch(
      std::vector<escher::TexturePtr>{},
      {std::move(stroke_info_buffer),
       std::move(control_points_buffer),
       std::move(re_params_buffer),
       std::move(division_counts_buffer),
       std::move(cumulative_division_counts_buffer),
       std::move(vertex_buffer),
       std::move(index_buffer)},
      command, group_count,
      /* group_count_y= */ 1, /* group_count_z= */ 1,
      /* push_constants= */ nullptr);
}

}  // namespace sketchy_service
