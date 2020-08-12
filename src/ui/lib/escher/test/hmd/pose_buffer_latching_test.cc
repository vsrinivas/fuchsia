// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/hmd/pose_buffer_latching_shader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/scene/camera.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/epsilon_compare.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

#include <glm/gtc/type_ptr.hpp>

namespace escher {
namespace test {

// Returns true iff |a| and |b| are the same within optional |epsilon|.
bool ComparePose(escher::hmd::Pose* p0, escher::hmd::Pose* p1, float epsilon = 0.0) {
  bool compare = true;

  EXPECT_TRUE(CompareFloat(p0->a, p1->a, epsilon));
  compare = compare && CompareFloat(p0->a, p1->a, epsilon);

  EXPECT_TRUE(CompareFloat(p0->b, p1->b, epsilon));
  compare = compare && CompareFloat(p0->b, p1->b, epsilon);

  EXPECT_TRUE(CompareFloat(p0->c, p1->c, epsilon));
  compare = compare && CompareFloat(p0->c, p1->c, epsilon);

  EXPECT_TRUE(CompareFloat(p0->d, p1->d, epsilon));
  compare = compare && CompareFloat(p0->d, p1->d, epsilon);

  EXPECT_TRUE(CompareFloat(p0->x, p1->x, epsilon));
  compare = compare && CompareFloat(p0->x, p1->x, epsilon);

  EXPECT_TRUE(CompareFloat(p0->y, p1->y, epsilon));
  compare = compare && CompareFloat(p0->y, p1->y, epsilon);

  EXPECT_TRUE(CompareFloat(p0->z, p1->z, epsilon));
  compare = compare && CompareFloat(p0->z, p1->z, epsilon);

  return true;
}

glm::mat4 MatrixFromPose(const hmd::Pose& pose) {
  return glm::toMat4(glm::quat(pose.d, pose.a, pose.b, pose.c)) *
         glm::translate(mat4(), glm::vec3(pose.x, pose.y, pose.z));
}

using PoseBufferTest = escher::test::TestWithVkValidationLayer;

// TODO(fxbug.dev/36692): This test now causes Vulkan validation errors on AEMU.
VK_TEST_F(PoseBufferTest, ComputeShaderLatching) {
  auto escher = escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
  escher->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      escher::hmd::kPoseBufferLatchingPaths);

  escher::FramePtr frame =
      escher->NewFrame("PoseBufferLatchingTest", 0, false, escher::CommandBuffer::Type::kCompute);

  uint32_t num_entries = 8;
  uint64_t base_time = 42L;              // Choose an arbitrary, non-zero time.
  uint64_t time_interval = 1024 * 1024;  // 1 ms

  vk::DeviceSize pose_buffer_size = num_entries * sizeof(escher::hmd::Pose);
  vk::MemoryPropertyFlags memory_property_flags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
  vk::BufferUsageFlags buffer_usage_flags =
      vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer;

  // Create the shader.
  hmd::PoseBuffer pose_buffer(
      escher->gpu_allocator()->AllocateBuffer(escher->resource_recycler(), pose_buffer_size,
                                              buffer_usage_flags, memory_property_flags),
      num_entries, base_time, time_interval);

  hmd::PoseBufferLatchingShader test_shader(escher->GetWeakPtr());

  // Fill the pose buffer.
  ASSERT_NE(nullptr, pose_buffer.buffer->host_ptr());
  hmd::Pose* poses = reinterpret_cast<hmd::Pose*>(pose_buffer.buffer->host_ptr());
  float pi = glm::pi<float>();
  for (uint32_t i = 0; i < num_entries; i++) {
    // Change pose each interation. The goal is to have unique poses in each
    // slot of the buffer where the first pose is the identity pose.
    glm::vec3 pos = vec3(i * 3.f, i * 5.f, i * 7.f);
    vec3 euler_angles(2 * pi / num_entries * i);
    glm::quat quat(euler_angles);
    new (&poses[i]) escher::hmd::Pose(quat, pos);
  }

  // Dispatch shaders.
  std::vector<BufferPtr> output_buffers;
  std::vector<Camera> cameras;
  // Dispatch a few extra to test modulo rollover.
  uint32_t num_dispatches = num_entries * 2;
  for (uint32_t i = 0; i < num_dispatches; i++) {
    // Identity Camera.
    Camera camera(glm::mat4(1), glm::mat4(1));
    // Use identity camera for first iteration only, change for all others.
    if (i != 0) {
      camera = Camera(glm::rotate(glm::mat4(), 2 * pi / num_dispatches * i, vec3(1, 1, 1)),
                      glm::perspective(45.0f, 1.0f, 0.1f, 100.0f));
    }

    uint64_t latch_time = base_time + (time_interval * (0.5 + i));
    output_buffers.push_back(test_shader.LatchPose(frame, camera, pose_buffer, latch_time, true));
    cameras.push_back(camera);
  }

  // Dispatch the shader once to test the stereo flow.
  // This is kept simple as most of the functionality is tested above.
  // Identity Camera.
  Camera left_camera(glm::mat4(1), glm::mat4(2));
  Camera right_camera(glm::mat4(1), glm::mat4(3));
  BufferPtr stereo_output_buffer =
      test_shader.LatchStereoPose(frame, left_camera, right_camera, pose_buffer, base_time, true);

  // Execute shaders.
  frame->EndFrame(nullptr, []() {});
  auto result = escher->vk_device().waitIdle();
  ASSERT_EQ(vk::Result::eSuccess, result);

  // Verify results.
  for (uint32_t i = 0; i < num_dispatches; i++) {
    auto output_buffer = output_buffers[i];
    ASSERT_NE(nullptr, output_buffer->host_ptr());
    uint32_t index = i % num_entries;
    auto pose_in = &poses[index];
    auto pose_out = reinterpret_cast<hmd::Pose*>(output_buffer->host_ptr());
    EXPECT_TRUE(ComparePose(pose_in, pose_out, 0.0));
    glm::mat4 vp_matrix_in =
        cameras[i].projection() * MatrixFromPose(*pose_in) * cameras[i].transform();
    glm::mat4 vp_matrix_out =
        glm::make_mat4(reinterpret_cast<float*>(output_buffer->host_ptr() + sizeof(hmd::Pose)));
    EXPECT_TRUE(CompareMatrix(vp_matrix_in, vp_matrix_out, 0.00001));

    // Pose zero uses all identity params so VP result should be identity.
    if (i == 0) {
      EXPECT_TRUE(CompareMatrix(mat4(), vp_matrix_out, 0.0));
    }
  }

  // Stereo flow:
  glm::mat4 left_vp_matrix_in =
      left_camera.projection() * MatrixFromPose(poses[0]) * left_camera.transform();
  glm::mat4 left_vp_matrix_out = glm::make_mat4(
      reinterpret_cast<float*>(stereo_output_buffer->host_ptr() + sizeof(hmd::Pose)));
  EXPECT_TRUE(CompareMatrix(left_vp_matrix_in, left_vp_matrix_out, 0.00001));

  glm::mat4 right_vp_matrix_in =
      right_camera.projection() * MatrixFromPose(poses[0]) * right_camera.transform();
  glm::mat4 right_vp_matrix_out = glm::make_mat4(reinterpret_cast<float*>(
      stereo_output_buffer->host_ptr() + sizeof(hmd::Pose) + 16 * sizeof(float)));
  EXPECT_TRUE(CompareMatrix(right_vp_matrix_in, right_vp_matrix_out, 0.00001));

  escher->Cleanup();
}

}  // namespace test
}  // namespace escher
