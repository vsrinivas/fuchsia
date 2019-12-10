// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/naive_image.h"

#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {

using NaiveImageTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(NaiveImageTest, AdoptVkImageInsufficientMemory) {
  auto escher = test::GetEscher();
  auto allocator = escher->gpu_allocator();
  auto recycler = escher->resource_recycler();

  FXL_LOG(INFO) << "Testing creation of NaiveImage with insufficient memory. "
                   "Error messages are expected.";

  // First we create a VkImage requiring a large amount of memory.
  ImageInfo large_image_info = {
      .format = vk::Format::eB8G8R8A8Unorm,
      .width = 1024,
      .height = 1024,
      .sample_count = 1,
      .usage = vk::ImageUsageFlagBits::eSampled,
      .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
  };
  vk::Image vk_image = image_utils::CreateVkImage(escher->vk_device(), large_image_info,
                                                  vk::ImageLayout::eUndefined);

  // Then we manually set the memory requirements to a small size so that it is
  // unlikely that |vk_image| can use the allocated memory.
  auto mem_requirements = escher->vk_device().getImageMemoryRequirements(vk_image);
  auto image_required_mem_size = mem_requirements.size;
  mem_requirements.size = 1024;
  auto memory =
      allocator->AllocateMemory(mem_requirements, vk::MemoryPropertyFlagBits::eDeviceLocal);

  // The size of allocated memory should be larger than the requirement of
  // |vk_image|.
  auto naive_image = impl::NaiveImage::AdoptVkImage(recycler, large_image_info, vk_image, memory,
                                                    vk::ImageLayout::eUndefined);
  EXPECT_GT(image_required_mem_size, memory->size());

  // AdoptVkImage() should fail and return nullptr instead.
  EXPECT_FALSE(naive_image);
}

}  // namespace escher
