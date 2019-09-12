// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class VkSessionTest : public SessionTest {
 public:
  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues();
  static vk::DeviceMemory AllocateExportableMemory(vk::Device device,
                                                   vk::PhysicalDevice physical_device,
                                                   vk::MemoryRequirements requirements,
                                                   vk::MemoryPropertyFlags flags);
  static zx::vmo ExportMemoryAsVmo(vk::Device device, vk::DispatchLoaderDynamic dispatch_loader,
                                   vk::DeviceMemory memory);
  static vk::MemoryRequirements GetBufferRequirements(vk::Device device, vk::DeviceSize size,
                                                      vk::BufferUsageFlags usage_flags);

  void SetUp() override;
  void TearDown() override;

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  DisplayManager* display_manager() { return display_manager_.get(); }

 protected:
  // |SessionTest|
  SessionContext CreateSessionContext() override;
  // |SessionTest|
  CommandContext CreateCommandContext() override;

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_
