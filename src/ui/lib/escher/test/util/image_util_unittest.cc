// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace {
using namespace escher;

const vk::DeviceSize kTestMemorySize = 10000;
const uint32_t kWidth = 16;
const uint32_t kHeight = 32;

using ImageUtilTest = test::TestWithVkValidationLayer;

// Check to make sure we return nullptr for an image when the memory we are
// supplying the image is too small to meet the memory requirements determined
// by the width/height set in vk::ImageCreateInfo.
VK_TEST_F(ImageUtilTest, SizeTooLargeTest) {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  auto vulkan_queues =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanDevice();
  auto device = vulkan_queues->GetVulkanContext().device;
  auto physical_device = vulkan_queues->GetVulkanContext().physical_device;

  auto resource_recycler = escher::test::GetEscher()->resource_recycler();

  vk::MemoryAllocateInfo info;
  info.allocationSize = kTestMemorySize;
  info.memoryTypeIndex = impl::GetMemoryTypeIndex(physical_device, INT32_MAX,
                                                  vk::MemoryPropertyFlagBits::eHostVisible);
  vk::DeviceMemory vk_mem = ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));

  // This test only checks for valid creation and destruction. It would need
  // a mock Vulkan to test for memory usage.
  auto mem = GpuMem::AdoptVkMemory(device, vk_mem, kTestMemorySize, true /* needs_mapped_ptr */);

  // Set the ImageCreateInfo struct information.
  vk::ImageCreateInfo create_info;
  create_info.format = vk::Format::eB8G8R8A8Unorm;
  create_info.imageType = vk::ImageType::e2D;
  create_info.extent = vk::Extent3D{kWidth, kHeight, 1};
  create_info.flags = vk::ImageCreateFlagBits::eMutableFormat;
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;

  // This should be true, as the memory is big enough to fit the image.
  {
    auto image = image_utils::NewImage(device, create_info, mem, resource_recycler);
    EXPECT_TRUE(image);
  }

  // This should be false, as the image dimensions are too large, resulting in the required
  // memory being more than the passed in escher::GpuMemPtr can provide.
  {
    create_info.extent = vk::Extent3D{kWidth * 4, kHeight * 4, 1};
    auto image = image_utils::NewImage(device, create_info, mem, resource_recycler);
    EXPECT_FALSE(image);
  }
}

}  // anonymous namespace
