// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands.h>
#include <zircon/syscalls.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_util.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using PoseBufferTest = VkSessionTest;

VK_TEST_F(PoseBufferTest, Validation) {
  const ResourceId invalid_id = 0;
  const ResourceId scene_id = 1;
  const ResourceId camera_id = 2;
  const ResourceId memory_id = 3;
  const ResourceId buffer_id = 4;

  ASSERT_TRUE(Apply(scenic::NewCreateSceneCmd(scene_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateCameraCmd(camera_id, scene_id)));

  const size_t kVmoSize = PAGE_SIZE;

  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // TODO(fxbug.dev/24563): Scenic may use a different set of bits when creating a
  // buffer, resulting in a memory pool mismatch.
  const vk::BufferUsageFlags kUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer;

  auto memory_requirements = GetBufferRequirements(device, kVmoSize, kUsageFlags);
  auto memory = AllocateExportableMemory(
      device, physical_device, memory_requirements,
      vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible);

  // If we can't make memory that is both host-visible and device-local, we
  // can't run this test.
  if (!memory) {
    FX_LOGS(INFO) << "Could not find UMA compatible memory pool, aborting test.";
    return;
  }

  zx::vmo vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  zx_clock_t base_time = dispatcher_clock_now();
  uint64_t time_interval = 1024 * 1024;  // 1 ms
  uint32_t num_entries = 1;

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  ASSERT_TRUE(Apply(scenic::NewCreateBufferCmd(buffer_id, memory_id, 0, kVmoSize)));

  // Actual Tests

  // Basic case: all arguments valid
  EXPECT_TRUE(Apply(scenic::NewSetCameraPoseBufferCmd(camera_id, buffer_id, num_entries, base_time,
                                                      time_interval)));

  // Basic case: using zx::time and zx::duration
  EXPECT_TRUE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, num_entries, zx::time(base_time), zx::duration(time_interval))));

  // Invalid base time 1 second in the future
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, num_entries, base_time + 1024 * 1024 * 1024, time_interval)));

  // Invalid case: using zx::time and zx::duration
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(camera_id, buffer_id, num_entries,
                                                       zx::time(base_time + 1024 * 1024 * 1024),
                                                       zx::duration(time_interval))));

  // Invalid buffer id
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(camera_id, invalid_id, num_entries,
                                                       base_time, time_interval)));

  // Invalid camera id
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(invalid_id, buffer_id, num_entries,
                                                       base_time, time_interval)));

  // num_entries too small
  EXPECT_FALSE(
      Apply(scenic::NewSetCameraPoseBufferCmd(camera_id, buffer_id, 0, base_time, time_interval)));

  // num_entries too large
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(camera_id, buffer_id, UINT32_MAX, base_time,
                                                       time_interval)));

  device.freeMemory(memory);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
