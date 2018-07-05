// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "public/lib/escher/test/gtest_vulkan.h"

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

using PoseBufferTest = VkSessionTest;

VK_TEST_F(PoseBufferTest, Validation) {
  const scenic::ResourceId invalid_id = 0;
  const scenic::ResourceId scene_id = 1;
  const scenic::ResourceId camera_id = 2;
  const scenic::ResourceId memory_id = 3;
  const scenic::ResourceId buffer_id = 4;

  ASSERT_TRUE(Apply(scenic::NewCreateSceneCmd(scene_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateCameraCmd(camera_id, scene_id)));

  uint64_t vmo_size = PAGE_SIZE;
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t base_time = zx::clock::get<ZX_CLOCK_MONOTONIC>().get();
  uint64_t time_interval = 1024 * 1024;  // 1 ms
  uint32_t num_entries = 1;

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(
      memory_id, std::move(vmo),
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  ASSERT_TRUE(Apply(
      scenic::NewCreateBufferCmd(buffer_id, memory_id, 0, vmo_size)));

  // Actual Tests

  // Basic case: all arguments valid
  EXPECT_TRUE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, num_entries, base_time, time_interval)));

  // Invalid base time in the future
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, num_entries, UINT64_MAX, time_interval)));

  // Invalid buffer id
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, invalid_id, num_entries, base_time, time_interval)));

  // Invalid camera id
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      invalid_id, buffer_id, num_entries, base_time, time_interval)));

  // num_entries too small
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, 0, base_time, time_interval)));

  // num_entries too large
  EXPECT_FALSE(Apply(scenic::NewSetCameraPoseBufferCmd(
      camera_id, buffer_id, UINT32_MAX, base_time, time_interval)));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic