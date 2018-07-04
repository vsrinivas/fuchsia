// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/stroke/stroke_tessellator.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/profiling/timestamp_profiler.h"
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

layout(std430, binding = 5) buffer DivisionSegmentIndices {
  uint division_segment_indices[];
};

layout(std430, binding = 6) buffer Vertices {
  Vertex vertices[];
};

layout(std430, binding = 7) buffer Indices {
  uint indices[];
};

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

  uint segment_idx = division_segment_indices[division_idx];
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

void SetUpMemoryBarrier(const escher::BufferPtr& buffer,
                        const vk::AccessFlagBits& src_access_mask,
                        const vk::AccessFlagBits& dst_access_mask,
                        vk::BufferMemoryBarrier* barrier) {
  barrier->srcAccessMask = src_access_mask;
  barrier->dstAccessMask = dst_access_mask;
  barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier->buffer = buffer->vk();
  barrier->offset = 0;
  barrier->size = buffer->size();
}

}  // namespace

namespace sketchy_service {

StrokeTessellator::StrokeTessellator(escher::EscherWeakPtr escher)
    : kernel_(std::move(escher), std::vector<vk::ImageLayout>{},
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
                  // Binding 5: |division_segment_index_buffer|
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 6: output vertex buffer
                  vk::DescriptorType::eStorageBuffer,
                  // Binding 7: output index buffer
                  vk::DescriptorType::eStorageBuffer},
              /* push_constants_size= */ 0, kShaderCode) {}

void StrokeTessellator::Dispatch(
    const escher::BufferPtr& stroke_info_buffer,
    const escher::BufferPtr& control_points_buffer,
    const escher::BufferPtr& re_params_buffer,
    const escher::BufferPtr& division_counts_buffer,
    const escher::BufferPtr& cumulative_division_counts_buffer,
    const escher::BufferPtr& division_segment_index_buffer,
    escher::BufferPtr vertex_buffer, const escher::BufferRange& vertex_range,
    escher::BufferPtr index_buffer, const escher::BufferRange& index_range,
    escher::impl::CommandBuffer* command, escher::TimestampProfiler* profiler,
    uint32_t division_count, bool apply_barrier) {
  if (profiler) {
    profiler->AddTimestamp(command, vk::PipelineStageFlagBits::eBottomOfPipe,
                           "Before Tessellation");
  }

  if (apply_barrier) {
    // Apply barriers if the compute shader depends on memory operations.
    // stroke_info_buffer is a uniform buffer that is visible to both host and
    // device, and the rest of them use device memory. Therefore, the access
    // flag for stroke_info_buffer is eHostWrite, and the rest of them are
    // eTransferWrite.
    constexpr int kInputBufferCnt = 6;
    vk::BufferMemoryBarrier barriers[kInputBufferCnt];
    SetUpMemoryBarrier(stroke_info_buffer, vk::AccessFlagBits::eHostWrite,
                       vk::AccessFlagBits::eShaderRead, barriers);
    SetUpMemoryBarrier(control_points_buffer,
                       vk::AccessFlagBits::eTransferWrite,
                       vk::AccessFlagBits::eShaderRead, barriers + 1);
    SetUpMemoryBarrier(re_params_buffer, vk::AccessFlagBits::eTransferWrite,
                       vk::AccessFlagBits::eShaderRead, barriers + 2);
    SetUpMemoryBarrier(division_counts_buffer,
                       vk::AccessFlagBits::eTransferWrite,
                       vk::AccessFlagBits::eShaderRead, barriers + 3);
    SetUpMemoryBarrier(cumulative_division_counts_buffer,
                       vk::AccessFlagBits::eTransferWrite,
                       vk::AccessFlagBits::eShaderRead, barriers + 4);
    SetUpMemoryBarrier(division_segment_index_buffer,
                       vk::AccessFlagBits::eTransferWrite,
                       vk::AccessFlagBits::eShaderRead, barriers + 5);
    command->vk().pipelineBarrier(
        vk::PipelineStageFlagBits::eHost | vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), 0,
        nullptr, kInputBufferCnt, barriers, 0, nullptr);
  }
  uint32_t group_count = (division_count + kLocalSize - 1) / kLocalSize;
  kernel_.DispatchWithRanges(
      std::vector<escher::TexturePtr>{},
      {stroke_info_buffer, control_points_buffer, re_params_buffer,
       division_counts_buffer, cumulative_division_counts_buffer,
       division_segment_index_buffer, std::move(vertex_buffer),
       std::move(index_buffer)},
      {{0, stroke_info_buffer->size()},
       {0, control_points_buffer->size()},
       {0, re_params_buffer->size()},
       {0, division_counts_buffer->size()},
       {0, cumulative_division_counts_buffer->size()},
       {0, division_segment_index_buffer->size()},
       {vertex_range.offset, vertex_range.size},
       {index_range.offset, index_range.size}},
      command, group_count,
      /* group_count_y= */ 1, /* group_count_z= */ 1,
      /* push_constants= */ nullptr);

  if (profiler) {
    profiler->AddTimestamp(command, vk::PipelineStageFlagBits::eBottomOfPipe,
                           "After Tessellation");
  }
}

}  // namespace sketchy_service
