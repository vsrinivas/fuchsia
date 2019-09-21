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

VulkanDeviceQueuesPtr VkSessionTest::CreateVulkanDeviceQueues() {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  // This extension is necessary to support exporting Vulkan memory to a VMO.
  return VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME, VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME},
       {},
       vk::SurfaceKHR()});
}

vk::DeviceMemory VkSessionTest::AllocateExportableMemory(vk::Device device,
                                                         vk::PhysicalDevice physical_device,
                                                         vk::MemoryRequirements requirements,
                                                         vk::MemoryPropertyFlags flags) {
  uint32_t memory_type_index =
      impl::GetMemoryTypeIndex(physical_device, requirements.memoryTypeBits, flags);
  vk::PhysicalDeviceMemoryProperties memory_types = physical_device.getMemoryProperties();
  if (memory_type_index == memory_types.memoryTypeCount) {
    return nullptr;
  }

  vk::ExportMemoryAllocateInfoKHR export_info;
  export_info.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;

  vk::MemoryAllocateInfo info;
  info.pNext = &export_info;
  info.allocationSize = requirements.size;
  info.memoryTypeIndex = memory_type_index;

  vk::DeviceMemory memory = ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));
  return memory;
}

zx::vmo VkSessionTest::ExportMemoryAsVmo(vk::Device device,
                                         vk::DispatchLoaderDynamic dispatch_loader,
                                         vk::DeviceMemory memory) {
  vk::MemoryGetZirconHandleInfoFUCHSIA export_memory_info(
      memory, vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  auto result = device.getMemoryZirconHandleFUCHSIA(export_memory_info, dispatch_loader);
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Failed to export vk::DeviceMemory as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

vk::MemoryRequirements VkSessionTest::GetBufferRequirements(vk::Device device, vk::DeviceSize size,
                                                            vk::BufferUsageFlags usage_flags) {
  // Create a temp buffer to find out memory requirements.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = size;
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer = escher::ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));
  auto retval = device.getBufferMemoryRequirements(vk_buffer);
  device.destroyBuffer(vk_buffer);
  return retval;
}

void VkSessionTest::SetUp() {
  vk_debug_report_callback_registry_.RegisterDebugReportCallbacks();
  SessionTest::SetUp();

  sysmem_ = std::make_unique<Sysmem>();
  display_manager_ = std::make_unique<DisplayManager>();
  constexpr float display_width = 1024;
  constexpr float display_height = 768;
  display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
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
