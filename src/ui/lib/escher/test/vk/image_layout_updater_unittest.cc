// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/image_layout_updater.h"

#include "gtest/gtest.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/naive_gpu_allocator.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

namespace {

// Allocate an 128x128 escher Image which is specifically used for the tests
// below.
ImagePtr Create128x128EscherImage(Escher *escher, NaiveGpuAllocator *allocator,
                                  vk::ImageUsageFlags usage) {
  constexpr vk::DeviceSize kImageSize = 128;
  ImageInfo image_info = {.format = vk::Format::eR8G8B8A8Unorm,
                          .width = kImageSize,
                          .height = kImageSize,
                          .sample_count = 1,
                          .usage = usage,
                          .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
                          .tiling = vk::ImageTiling::eOptimal};
  return allocator->AllocateImage(escher->resource_recycler(), image_info, nullptr);
}

}  // namespace

using ImageLayoutUpdaterTest = TestWithVkValidationLayer;

// The following test checks if ImageLayoutUpdater works correctly:
//
// (1) Create an image with layout ImageLayout::eUndefined.
// (2) Convert this image to layout ImageLayout::eTransferSrcOptimal.
// (3) Copy this image to a Vulkan buffer.
//
// Vulkan validation layer will generate the following validation error if layout is not updated
// correctly:
//    [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ] Submitted command buffer expects
//    VkImage to be in layout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL -- instead, current layout is
//    VK_IMAGE_LAYOUT_UNDEFINED.
//
// If ImageLayoutUpdater works correctly, there should be no Vulkan validation debug reports.
//
VK_TEST_F(ImageLayoutUpdaterTest, SetLayout) {
  constexpr vk::ImageLayout kOldLayout = vk::ImageLayout::eUndefined;
  constexpr vk::ImageLayout kNewLayout = vk::ImageLayout::eTransferSrcOptimal;

  Escher *escher = EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
  NaiveGpuAllocator naive_gpu_allocator(escher->vulkan_context());

  auto image =
      Create128x128EscherImage(escher, &naive_gpu_allocator, vk::ImageUsageFlagBits::eTransferSrc);
  EXPECT_EQ(image->layout(), kOldLayout);

  auto image_layout_updater = ImageLayoutUpdater::New(escher->GetWeakPtr());
  bool layout_updated = false;
  image_layout_updater->ScheduleSetImageInitialLayout(image, kNewLayout);
  image_layout_updater->Submit([&layout_updated]() { layout_updated = true; });
  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_EQ(image->layout(), kNewLayout);
  EXPECT_TRUE(layout_updated);

  // Test downloading the image to see if we set the image layout correctly.
  // If the layout is not set correctly we will see Vulkan validation errors.
  auto buffer = escher->NewBuffer(image->size(), vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal);
  vk::BufferImageCopy region;
  region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
  region.imageExtent = {.width = image->width(), .height = image->height(), .depth = 1};
  region.bufferOffset = 0;
  auto download_cmds = CommandBuffer::NewForTransfer(escher);
  download_cmds->vk().copyImageToBuffer(image->vk(), image->layout(), buffer->vk(), {region});
  download_cmds->Submit([]() {});

  escher->vk_device().waitIdle();
  EXPECT_VULKAN_VALIDATION_OK();
  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST_F(ImageLayoutUpdaterTest, SubmitToTransferCommandBuffer) {
  constexpr vk::ImageLayout kOldLayout = vk::ImageLayout::eUndefined;
  constexpr vk::ImageLayout kNewLayout = vk::ImageLayout::eTransferSrcOptimal;

  Escher *escher = EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
  NaiveGpuAllocator naive_gpu_allocator(escher->vulkan_context());

  auto image =
      Create128x128EscherImage(escher, &naive_gpu_allocator, vk::ImageUsageFlagBits::eTransferSrc);
  EXPECT_EQ(image->layout(), kOldLayout);

  auto image_layout_updater = ImageLayoutUpdater::New(escher->GetWeakPtr());
  auto cmds = CommandBuffer::NewForTransfer(escher);
  bool cmds_submitted = false;
  image_layout_updater->ScheduleSetImageInitialLayout(image, kNewLayout);
  image_layout_updater->GenerateCommands(cmds.get());
  cmds->Submit([&cmds_submitted]() { cmds_submitted = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_EQ(image->layout(), kNewLayout);
  EXPECT_TRUE(cmds_submitted);
}

VK_TEST_F(ImageLayoutUpdaterTest, SubmitToGraphicsCommandBuffer) {
  constexpr vk::ImageLayout kOldLayout = vk::ImageLayout::eUndefined;
  constexpr vk::ImageLayout kNewLayout = vk::ImageLayout::eTransferSrcOptimal;

  Escher *escher = EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
  NaiveGpuAllocator naive_gpu_allocator(escher->vulkan_context());

  auto image =
      Create128x128EscherImage(escher, &naive_gpu_allocator, vk::ImageUsageFlagBits::eTransferSrc);
  EXPECT_EQ(image->layout(), kOldLayout);

  auto image_layout_updater = ImageLayoutUpdater::New(escher->GetWeakPtr());
  auto cmds = CommandBuffer::NewForGraphics(escher, /* use_protected_memory */ false);
  bool cmds_submitted = false;
  image_layout_updater->ScheduleSetImageInitialLayout(image, kNewLayout);
  image_layout_updater->GenerateCommands(cmds.get());
  cmds->Submit([&cmds_submitted]() { cmds_submitted = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_EQ(image->layout(), kNewLayout);
  EXPECT_TRUE(cmds_submitted);
}

VK_TEST_F(ImageLayoutUpdaterTest, SetLayoutOnSameImageDeathTest) {
  constexpr vk::ImageLayout kOldLayout = vk::ImageLayout::eUndefined;
  constexpr vk::ImageLayout kNewLayout = vk::ImageLayout::eTransferSrcOptimal;

  Escher *escher = EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
  NaiveGpuAllocator naive_gpu_allocator(escher->vulkan_context());

  // Death test: We should not set the initial layout of the same image twice.
  ASSERT_DEATH(
      {
        auto image = Create128x128EscherImage(escher, &naive_gpu_allocator,
                                              vk::ImageUsageFlagBits::eTransferSrc);
        EXPECT_EQ(image->layout(), kOldLayout);
        auto image_layout_updater = ImageLayoutUpdater::New(escher->GetWeakPtr());
        image_layout_updater->ScheduleSetImageInitialLayout(image, kNewLayout);
        image_layout_updater->ScheduleSetImageInitialLayout(image, kNewLayout);
        image_layout_updater->Submit();
      },
      "Initial layout can be set only once for each image.");
  escher->vk_device().waitIdle();
}

}  // namespace test
}  // namespace escher
