// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/hmd/pose_buffer_latching_shader.h"

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/scene/camera.h"
#include "src/ui/lib/escher/shaders/util/spirv_file_util.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {
namespace hmd {

const std::vector<std::string> kPoseBufferLatchingPaths = {
    "shaders/compute/pose_buffer_latching.comp"};

const std::vector<std::string> kPoseBufferLatchingSpirvPaths = {
    "shaders/shaders_compute_pose_buffer_latching_comp14695981039346656037.spirv"};

const ShaderProgramData kPoseBufferLatchingProgramData = {
    .source_files = {{ShaderStage::kCompute, "shaders/compute/pose_buffer_latching.comp"}}};

static constexpr size_t k4x4MatrixSize = 16 * sizeof(float);

PoseBufferLatchingShader::PoseBufferLatchingShader(EscherWeakPtr escher)
    : escher_(std::move(escher)) {}

BufferPtr PoseBufferLatchingShader::LatchPose(const FramePtr& frame, const Camera& camera,
                                              PoseBuffer pose_buffer, int64_t latch_time,
                                              bool host_accessible_output) {
  return LatchStereoPose(frame, camera, camera, pose_buffer, latch_time, host_accessible_output);
}

BufferPtr PoseBufferLatchingShader::LatchStereoPose(const FramePtr& frame,
                                                    const Camera& left_camera,
                                                    const Camera& right_camera,
                                                    PoseBuffer pose_buffer, int64_t latch_time,
                                                    bool host_accessible_output) {
  vk::DeviceSize buffer_size = 2 * k4x4MatrixSize + sizeof(Pose);

  const vk::MemoryPropertyFlags kOutputMemoryPropertyFlags =
      host_accessible_output
          ? (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
          : (vk::MemoryPropertyFlagBits::eDeviceLocal);
  const vk::BufferUsageFlags kOutputBufferUsageFlags =
      vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer;

  auto output_buffer =
      frame->gpu_allocator()->AllocateBuffer(escher_->resource_recycler(), buffer_size,
                                             kOutputBufferUsageFlags, kOutputMemoryPropertyFlags);

  const vk::MemoryPropertyFlags kVpMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
  const vk::BufferUsageFlags kVpBufferUsageFlags = vk::BufferUsageFlagBits::eUniformBuffer;

  auto vp_matrices_buffer =
      frame->gpu_allocator()->AllocateBuffer(escher_->resource_recycler(), 4 * k4x4MatrixSize,
                                             kVpBufferUsageFlags, kVpMemoryPropertyFlags);

  auto command_buffer = frame->cmds();

  // This should be guaranteed by checks at a higher layer.  For example,
  // Scenic checks this in Session::ApplySetCameraPoseBufferCmd().
  FX_DCHECK(latch_time >= pose_buffer.base_time);

  uint32_t latch_index = static_cast<uint32_t>(
      ((latch_time - pose_buffer.base_time) / pose_buffer.time_interval) % pose_buffer.num_entries);

  FX_DCHECK(vp_matrices_buffer->host_ptr() != nullptr);
  glm::mat4* vp_matrices = reinterpret_cast<glm::mat4*>(vp_matrices_buffer->host_ptr());
  vp_matrices[0] = left_camera.transform();
  vp_matrices[1] = left_camera.projection();
  vp_matrices[2] = right_camera.transform();
  vp_matrices[3] = right_camera.projection();

  if (!program_) {
    program_ = escher_->GetProgram(kPoseBufferLatchingProgramData);
  }

  command_buffer->SetShaderProgram(program_, nullptr);
  command_buffer->PushConstants(latch_index, /*Offset*/ 0);

  command_buffer->BindUniformBuffer(0, 0, vp_matrices_buffer);
  command_buffer->BindUniformBuffer(0, 1, pose_buffer.buffer);
  command_buffer->BindUniformBuffer(0, 2, output_buffer);

  command_buffer->Dispatch(1, 1, 1);

  return output_buffer;
}
}  // namespace hmd
}  // namespace escher
