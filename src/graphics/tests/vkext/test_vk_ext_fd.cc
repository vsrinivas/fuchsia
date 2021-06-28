// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <gtest/gtest.h>

#include "src/graphics/tests/common/vulkan_context.h"

class TestBase : public testing::Test {
 public:
  TestBase(std::vector<const char*> desired_device_extensions)
      : desired_device_extensions_(std::move(desired_device_extensions)) {}

  void SetUp() override {
    auto app_info =
        vk::ApplicationInfo().setPApplicationName("test").setApiVersion(VK_API_VERSION_1_1);
    auto instance_info = vk::InstanceCreateInfo().setPApplicationInfo(&app_info);

    constexpr uint32_t kPhysicalDeviceIndex = 0;
    context_ = std::make_unique<VulkanContext>(kPhysicalDeviceIndex);
    context_->set_instance_info(instance_info);
    context_->set_validation_layers_enabled(false);
    ASSERT_TRUE(context_->InitInstance());

    loader_.init(*context_->instance(), vkGetInstanceProcAddr);
    ASSERT_TRUE(context_->InitQueueFamily());

    {
      uint32_t ext_count = 0;
      auto [result, extensions] = context_->physical_device().enumerateDeviceExtensionProperties();
      EXPECT_EQ(result, vk::Result::eSuccess);

      for (auto& extension : extensions) {
        for (auto& desired : desired_device_extensions_) {
          if (strcmp(extension.extensionName, desired) == 0) {
            ext_count++;
          }
        }
      }
      if (ext_count != desired_device_extensions_.size()) {
        fprintf(stderr, "Missing extension(s)\n");
        GTEST_SKIP();
      }
    }

    auto device_create_info = vk::DeviceCreateInfo()
                                  .setQueueCreateInfoCount(1)
                                  .setPQueueCreateInfos(&context_->queue_info())
                                  .setPEnabledExtensionNames(desired_device_extensions_);
    context_->set_device_info(std::move(device_create_info));
    ASSERT_TRUE(context_->InitDevice());
  }

  std::vector<const char*> desired_device_extensions_;
  std::unique_ptr<VulkanContext> context_;
  vk::DispatchLoaderDynamic loader_;
};

// Test the vulkan semaphore external fd extension.
class TestVkExtSemFd : public TestBase {
 public:
  TestVkExtSemFd()
      : TestBase({VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                  VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME}) {}
};

TEST_F(TestVkExtSemFd, SemaphoreExportThenImport) {
  auto export_create_info = vk::ExportSemaphoreCreateInfo().setHandleTypes(
      vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd);
  auto create_info = vk::SemaphoreCreateInfo().setPNext(&export_create_info);
  auto ret = context_->device()->createSemaphore(create_info);
  ASSERT_EQ(vk::Result::eSuccess, ret.result);
  vk::Semaphore& sem_export = ret.value;

  int fd;

  {
    auto semaphore_get_info =
        vk::SemaphoreGetFdInfoKHR()
            .setSemaphore(sem_export)
            .setHandleType(vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd);
    auto export_ret = context_->device()->getSemaphoreFdKHR(semaphore_get_info, loader_);
    ASSERT_EQ(vk::Result::eSuccess, export_ret.result);
    fd = export_ret.value;
  }

  // TODO(fxbug.dev/67565) - check non negative
  EXPECT_NE(0, fd);

  {
    auto ret = context_->device()->createSemaphore(create_info);
    ASSERT_EQ(vk::Result::eSuccess, ret.result);
    vk::Semaphore& sem_import = ret.value;

    auto semaphore_import_info =
        vk::ImportSemaphoreFdInfoKHR()
            .setSemaphore(sem_import)
            .setHandleType(vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd)
            .setFd(fd);
    auto import_ret = context_->device()->importSemaphoreFdKHR(semaphore_import_info, loader_);
    ASSERT_EQ(vk::Result::eSuccess, import_ret);
  }
}

TEST_F(TestVkExtSemFd, FenceExportThenImport) {
  auto export_create_info =
      vk::ExportFenceCreateInfo().setHandleTypes(vk::ExternalFenceHandleTypeFlagBits::eOpaqueFd);
  auto create_info = vk::FenceCreateInfo().setPNext(&export_create_info);
  auto ret = context_->device()->createFence(create_info);
  ASSERT_EQ(vk::Result::eSuccess, ret.result);
  vk::Fence& fence_export = ret.value;

  int fd;

  {
    auto fence_get_info = vk::FenceGetFdInfoKHR()
                              .setFence(fence_export)
                              .setHandleType(vk::ExternalFenceHandleTypeFlagBits::eOpaqueFd);
    auto export_ret = context_->device()->getFenceFdKHR(fence_get_info, loader_);
    ASSERT_EQ(vk::Result::eSuccess, export_ret.result);
    fd = export_ret.value;
  }

  // TODO(fxbug.dev/67565) - check non negative
  EXPECT_NE(0, fd);

  {
    auto ret = context_->device()->createFence(create_info);
    ASSERT_EQ(vk::Result::eSuccess, ret.result);
    vk::Fence& fence_import = ret.value;

    auto fence_import_info = vk::ImportFenceFdInfoKHR()
                                 .setFence(fence_import)
                                 .setHandleType(vk::ExternalFenceHandleTypeFlagBits::eOpaqueFd)
                                 .setFd(fd);
    auto import_ret = context_->device()->importFenceFdKHR(fence_import_info, loader_);
    ASSERT_EQ(vk::Result::eSuccess, import_ret);
  }
}

// Test the vulkan memory external fd extension.
class TestVkExtMemFd : public TestBase {
 public:
  TestVkExtMemFd() : TestBase({VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME}) {}
};

TEST_F(TestVkExtMemFd, ImageExport) {
  constexpr uint32_t kDefaultWidth = 64;
  constexpr uint32_t kDefaultHeight = 64;
  constexpr vk::Format kDefaultVkFormat = vk::Format::eB8G8R8A8Unorm;

  vk::UniqueImage image;
  vk::UniqueDeviceMemory memory;

  {
    auto external_create_info = vk::ExternalMemoryImageCreateInfo().setHandleTypes(
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);

    auto create_info = vk::ImageCreateInfo()
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(kDefaultVkFormat)
                           .setExtent(vk::Extent3D(kDefaultWidth, kDefaultHeight, 1))
                           .setMipLevels(1)
                           .setArrayLayers(1)
                           .setSamples(vk::SampleCountFlagBits::e1)
                           .setTiling(vk::ImageTiling::eOptimal)
                           .setUsage(vk::ImageUsageFlagBits::eTransferSrc)
                           .setSharingMode(vk::SharingMode::eExclusive)
                           .setInitialLayout(vk::ImageLayout::ePreinitialized)
                           .setPNext(&external_create_info);

    auto result = context_->device()->createImageUnique(create_info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    image = std::move(result.value);
  }

  {
    auto memory_reqs_chain =
        context_->device()
            ->getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                image.get());
    EXPECT_TRUE(
        memory_reqs_chain.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation);

    auto& mem_reqs = memory_reqs_chain.get<vk::MemoryRequirements2>().memoryRequirements;
    uint32_t memory_type_index = __builtin_ctz(mem_reqs.memoryTypeBits);

    auto dedicated_create_info = vk::MemoryDedicatedAllocateInfo().setImage(image.get());

    auto export_create_info =
        vk::ExportMemoryAllocateInfo()
            .setHandleTypes(vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd)
            .setPNext(&dedicated_create_info);

    auto alloc_info = vk::MemoryAllocateInfo()
                          .setAllocationSize(mem_reqs.size)
                          .setMemoryTypeIndex(memory_type_index)
                          .setPNext(&export_create_info);

    auto result = context_->device()->allocateMemoryUnique(alloc_info);
    ASSERT_EQ(result.result, vk::Result::eSuccess);
    memory = std::move(result.value);

    ASSERT_EQ(vk::Result::eSuccess,
              context_->device()->bindImageMemory(image.get(), memory.get(), 0u));
  }

  {
    auto get_fd_info = vk::MemoryGetFdInfoKHR()
                           .setMemory(memory.get())
                           .setHandleType(vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);
    auto result = context_->device()->getMemoryFdKHR(get_fd_info, loader_);
    ASSERT_EQ(result.result, vk::Result::eSuccess);
  }
}
