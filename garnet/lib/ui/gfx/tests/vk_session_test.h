// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_

#include "garnet/lib/ui/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class VkSessionTest : public SessionTest {
 public:
  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues();
  static vk::DeviceMemory AllocateExportableMemory(
      vk::Device device, vk::PhysicalDevice physical_device,
      vk::MemoryRequirements requirements, vk::MemoryPropertyFlags flags);
  static zx::vmo ExportMemoryAsVmo(vk::Device device,
                                   vk::DispatchLoaderDynamic dispatch_loader,
                                   vk::DeviceMemory memory);
  static vk::MemoryRequirements GetBufferRequirements(
      vk::Device device, vk::DeviceSize size, vk::BufferUsageFlags usage_flags);

  // |SessionTest|
  std::unique_ptr<SessionForTest> CreateSession() override;

  // This function provides a mechanism for tests to inject their own objects
  // into the SessionContext before construction.
  virtual void OnSessionContextCreated(SessionContext* context) {}

 protected:
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_
