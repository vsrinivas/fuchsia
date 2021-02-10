// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <gtest/gtest.h>

#include "src/graphics/tests/common/vulkan_context.h"

// Test the vulkan semaphore external fd extension.
class TestVkExtFd : public testing::Test {
 public:
  void SetUp() override {
    auto app_info =
        vk::ApplicationInfo().setPApplicationName("test").setApplicationVersion(VK_API_VERSION_1_1);
    auto instance_info = vk::InstanceCreateInfo().setPApplicationInfo(&app_info);

    std::array<const char*, 2> device_extensions{VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                                                 VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME};

    auto builder = VulkanContext::Builder();
    builder.set_instance_info(instance_info).set_validation_layers_enabled(false);
    builder.set_device_info(builder.DeviceInfo().setPEnabledExtensionNames(device_extensions));

    context_ = builder.Unique();
    ASSERT_TRUE(context_);

    loader_.init(*context_->instance(), vkGetInstanceProcAddr);
  }

  std::unique_ptr<VulkanContext> context_;
  vk::DispatchLoaderDynamic loader_;
};

TEST_F(TestVkExtFd, SemaphoreExportThenImport) {
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

TEST_F(TestVkExtFd, FenceExportThenImport) {
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
