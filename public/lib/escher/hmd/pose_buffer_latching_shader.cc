// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/hmd/pose_buffer_latching_shader.h"

#include "garnet/public/lib/escher/escher.h"
#include "garnet/public/lib/escher/hmd/pose_buffer.h"
#include "garnet/public/lib/escher/renderer/frame.h"
#include "garnet/public/lib/escher/resources/resource_recycler.h"
#include "garnet/public/lib/escher/scene/camera.h"
#include "garnet/public/lib/escher/vk/buffer.h"
#include "garnet/public/lib/escher/vk/texture.h"

namespace escher {
namespace hmd {

namespace {
constexpr char g_kernel_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  struct Pose {
    vec4 quaternion;
    vec3 position;
    uint reserved;
  };

  layout(push_constant) uniform PushConstants {
    uint latch_index;
  };

  layout (binding = 0) uniform VPMatrices {
    mat4 left_view_transform;
    mat4 left_projection_matrix;
    mat4 right_view_transform;
    mat4 right_projection_matrix;
  };

  layout (binding = 1) buffer PoseBuffer {
    Pose poses[];
  };

  layout (binding = 2) buffer OutputBuffer {
    Pose latched_pose;
    mat4 left_vp_matrix;
    mat4 right_vp_matrix;
  };

  // interpreted from GLM's mat3_cast
  mat3 quaternion_to_mat3(vec4 q)
  {
    mat3 result;
    float qxx = q.x * q.x;
    float qyy = q.y * q.y;
    float qzz = q.z * q.z;
    float qxz = q.x * q.z;
    float qxy = q.x * q.y;
    float qyz = q.y * q.z;
    float qwx = q.w * q.x;
    float qwy = q.w * q.y;
    float qwz = q.w * q.z;

    result[0][0] = float(1) - float(2) * (qyy +  qzz);
    result[0][1] = float(2) * (qxy + qwz);
    result[0][2] = float(2) * (qxz - qwy);

    result[1][0] = float(2) * (qxy - qwz);
    result[1][1] = float(1) - float(2) * (qxx +  qzz);
    result[1][2] = float(2) * (qyz + qwx);

    result[2][0] = float(2) * (qxz + qwy);
    result[2][1] = float(2) * (qyz - qwx);
    result[2][2] = float(1) - float(2) * (qxx +  qyy);

    return result;
  }

  mat4 translate(vec3 t){
    return mat4(
        vec4(1.0, 0.0, 0.0, 0.0),
        vec4(0.0, 1.0, 0.0, 0.0),
        vec4(0.0, 0.0, 1.0, 0.0),
        vec4(t.x, t.y, t.z, 1.0)
    );
  }

  void main() {
    latched_pose = poses[latch_index];
    left_vp_matrix = left_projection_matrix *
                mat4(quaternion_to_mat3(latched_pose.quaternion)) *
                translate(latched_pose.position) * left_view_transform;

    right_vp_matrix = right_projection_matrix *
                mat4(quaternion_to_mat3(latched_pose.quaternion)) *
                translate(latched_pose.position) * right_view_transform;
  }
  )GLSL";
}

static constexpr size_t k4x4MatrixSize = 16 * sizeof(float);

PoseBufferLatchingShader::PoseBufferLatchingShader(EscherWeakPtr escher)
    : escher_(std::move(escher)) {}

BufferPtr PoseBufferLatchingShader::LatchPose(const FramePtr& frame,
                                              const Camera& camera,
                                              PoseBuffer pose_buffer,
                                              uint64_t latch_time,
                                              bool host_accessible_output) {
  return LatchStereoPose(frame, camera, camera, pose_buffer, latch_time,
                         host_accessible_output);
}

BufferPtr PoseBufferLatchingShader::LatchStereoPose(
    const FramePtr& frame, const Camera& left_camera,
    const Camera& right_camera, PoseBuffer pose_buffer, uint64_t latch_time,
    bool host_accessible_output) {
  vk::DeviceSize buffer_size = 2 * k4x4MatrixSize + sizeof(Pose);

  const vk::MemoryPropertyFlags kOutputMemoryPropertyFlags =
      host_accessible_output ? (vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent)
                             : (vk::MemoryPropertyFlagBits::eDeviceLocal);
  const vk::BufferUsageFlags kOutputBufferUsageFlags =
      vk::BufferUsageFlagBits::eUniformBuffer |
      vk::BufferUsageFlagBits::eStorageBuffer;

  auto output_buffer = Buffer::New(
      escher_->resource_recycler(), frame->gpu_allocator(), buffer_size,
      kOutputBufferUsageFlags, kOutputMemoryPropertyFlags);

  const vk::MemoryPropertyFlags kVpMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible |
      vk::MemoryPropertyFlagBits::eHostCoherent;
  const vk::BufferUsageFlags kVpBufferUsageFlags =
      vk::BufferUsageFlagBits::eUniformBuffer;

  auto vp_matrices_buffer = Buffer::New(
      escher_->resource_recycler(), frame->gpu_allocator(), 4 * k4x4MatrixSize,
      kVpBufferUsageFlags, kVpMemoryPropertyFlags);

  auto command_buffer = frame->command_buffer();

  uint32_t latch_index =
      ((latch_time - pose_buffer.base_time) / pose_buffer.time_interval) %
      pose_buffer.num_entries;

  FXL_DCHECK(vp_matrices_buffer->ptr() != nullptr);
  glm::mat4* vp_matrices =
      reinterpret_cast<glm::mat4*>(vp_matrices_buffer->ptr());
  vp_matrices[0] = left_camera.transform();
  vp_matrices[1] = left_camera.projection();
  vp_matrices[2] = right_camera.transform();
  vp_matrices[3] = right_camera.projection();

  if (!kernel_) {
    kernel_ = std::make_unique<impl::ComputeShader>(
        escher_, std::vector<vk::ImageLayout>{},
        std::vector<vk::DescriptorType>{vk::DescriptorType::eUniformBuffer,
                                        vk::DescriptorType::eStorageBuffer,
                                        vk::DescriptorType::eStorageBuffer},
        sizeof(latch_index), g_kernel_src);
  }

  std::vector<TexturePtr> textures;
  std::vector<BufferPtr> buffers;
  buffers.push_back(vp_matrices_buffer);
  buffers.push_back(pose_buffer.buffer);
  buffers.push_back(output_buffer);

  kernel_->Dispatch(textures, buffers, command_buffer, 1, 1, 1, &latch_index);

  return output_buffer;
}
}  // namespace hmd
}  // namespace escher
