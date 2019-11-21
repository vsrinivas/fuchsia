// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk_debug_report_collector.h"

using namespace escher;

namespace scenic_impl {
namespace gfx {
namespace test {

VulkanDeviceQueuesPtr VkSessionTest::CreateVulkanDeviceQueues(bool use_protected_memory) {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  // This extension is necessary to support exporting Vulkan memory to a VMO.
  VulkanDeviceQueues::Params::Flags flags =
      use_protected_memory ? VulkanDeviceQueues::Params::kAllowProtectedMemory : 0;
  auto vulkan_queues = VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME},
       {},
       vk::SurfaceKHR(),
       flags});
  // Some devices might not be capable of using protected memory.
  if (use_protected_memory && !vulkan_queues->caps().allow_protected_memory) {
    return nullptr;
  }
  return vulkan_queues;
}

void VkSessionTest::SetUp() {
  vk_debug_report_callback_registry_.RegisterDebugReportCallbacks();
  SessionTest::SetUp();

  sysmem_ = std::make_unique<Sysmem>();
  display_manager_ = std::make_unique<display::DisplayManager>();
  constexpr float display_width = 1024;
  constexpr float display_height = 768;
  display_manager_->SetDefaultDisplayForTests(std::make_unique<display::Display>(
      /*id*/ 0, /*px-width*/ display_width, /*px-height*/ display_height));
}
void VkSessionTest::TearDown() {
  EXPECT_VULKAN_VALIDATION_OK();
  vk_debug_report_callback_registry_.DeregisterDebugReportCallbacks();
  SessionTest::TearDown();

  image_factory_.reset();
  release_fence_signaller_.reset();
  escher_.reset();
  sysmem_.reset();
  display_manager_.reset();
}

VkSessionTest::VkSessionTest()
    : SessionTest(),
      vk_debug_report_callback_registry_(
          escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance(),
          std::make_optional<VulkanInstance::DebugReportCallback>(
              escher::test::impl::VkDebugReportCollector::HandleDebugReport,
              &vk_debug_report_collector_),
          {}),
      vk_debug_report_collector_() {}

SessionContext VkSessionTest::CreateSessionContext() {
  auto session_context = SessionTest::CreateSessionContext();

  auto vulkan_device = CreateVulkanDeviceQueues();

  FXL_DCHECK(!escher_);
  FXL_DCHECK(!release_fence_signaller_);
  FXL_DCHECK(!image_factory_);

  escher_ = std::make_unique<Escher>(vulkan_device);
  release_fence_signaller_ =
      std::make_unique<ReleaseFenceSignaller>(escher_->command_buffer_sequencer());
  image_factory_ =
      std::make_unique<ImageFactoryAdapter>(escher_->gpu_allocator(), escher_->resource_recycler());

  session_context.vk_device = escher_->vk_device();
  session_context.escher = escher_.get();
  session_context.escher_resource_recycler = escher_->resource_recycler();
  session_context.escher_image_factory = image_factory_.get();
  session_context.release_fence_signaller = release_fence_signaller_.get();

  return session_context;
}

CommandContext VkSessionTest::CreateCommandContext() {
  return CommandContext(escher::BatchGpuUploader::New(escher_->GetWeakPtr(), /* trace_id = */ 0),
                        sysmem_.get(), display_manager_.get(), fxl::WeakPtr<SceneGraph>(/*empty*/));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
