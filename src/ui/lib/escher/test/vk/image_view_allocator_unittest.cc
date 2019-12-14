// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/vk/image_view.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

using ImageViewAllocatorTest = test::TestWithVkValidationLayer;

VK_TEST_F(ImageViewAllocatorTest, CacheReclamation) {
  auto escher = test::GetEscher();

  ImageViewAllocator allocator(escher->resource_recycler());
  allocator.BeginFrame();

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Get depth stencil texture format supported by the device.
  auto depth_stencil_texture_format_result =
      escher->device()->caps().GetMatchingDepthStencilFormat();
  if (depth_stencil_texture_format_result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "No depth stencil format is supported on this device.";
    return;
  }

  auto texture = escher->NewAttachmentTexture(depth_stencil_texture_format_result.value, width,
                                              height, 1, vk::Filter::eNearest);

  auto stencil_view =
      allocator.ObtainImageView(texture->image(), vk::ImageAspectFlagBits::eStencil);
  EXPECT_EQ(stencil_view,
            allocator.ObtainImageView(texture->image(), vk::ImageAspectFlagBits::eStencil));

  auto view = allocator.ObtainImageView(texture->image());
  // The imageview should be the same for the first frame
  EXPECT_EQ(view, allocator.ObtainImageView(texture->image()));

  // Different aspects should be tracked separately.
  EXPECT_NE(view, stencil_view);
  EXPECT_NE(view, allocator.ObtainImageView(texture->image(), vk::ImageAspectFlagBits::eDepth));

  // The ImageView should be the same for the following frame.
  allocator.BeginFrame();
  EXPECT_EQ(view, allocator.ObtainImageView(texture->image()));

  // ... in fact, ImageVews should not be evicted from the cache as long as
  // the number of frames since last use is < kFramesUntilEviction.
  constexpr uint32_t kNotEnoughFramesForEviction = 4;
  for (uint32_t i = 0; i < kNotEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_EQ(view, allocator.ObtainImageView(texture->image()));

  // ... but one more frame than that will cause a different ImageView to be
  // obtained from the allocator.
  constexpr uint32_t kJustEnoughFramesForEviction = kNotEnoughFramesForEviction + 1;
  for (uint32_t i = 0; i < kJustEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_NE(view, allocator.ObtainImageView(texture->image()));
}

}  // anonymous namespace
