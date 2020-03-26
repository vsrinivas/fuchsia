// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/vulkan_utils.h"

#include "src/ui/lib/escher/test/common/gtest_escher.h"

namespace {
using namespace escher;

bool IsEnclosedBy(const vk::Rect2D& rect, const vk::Rect2D& potential_encloser) {
  int64_t left, right, top, bottom, encloser_left, encloser_right, encloser_top, encloser_bottom;

  left = rect.offset.x;
  right = left + rect.extent.width;
  top = rect.offset.y;
  bottom = top + rect.extent.height;

  encloser_left = potential_encloser.offset.x;
  encloser_right = encloser_left + potential_encloser.extent.width;
  encloser_top = potential_encloser.offset.y;
  encloser_bottom = encloser_top + potential_encloser.extent.height;

  return left >= encloser_left && right <= encloser_right && top >= encloser_top &&
         bottom <= encloser_bottom;
}

TEST(VulkanUtils, ClipToRect) {
  vk::Rect2D rect, encloser{{1000, 1000}, {2000, 2000}};

  rect = vk::Rect2D({500, 500}, {3000, 3000});
  EXPECT_FALSE(IsEnclosedBy(rect, encloser));
  impl::ClipToRect(&rect, encloser);
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  EXPECT_EQ(rect, encloser);

  rect = vk::Rect2D({500, 500}, {2000, 2000});
  EXPECT_FALSE(IsEnclosedBy(rect, encloser));
  impl::ClipToRect(&rect, encloser);
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  EXPECT_NE(rect, encloser);
  EXPECT_EQ(rect, vk::Rect2D({1000, 1000}, {1500, 1500}));

  rect = vk::Rect2D({1200, 1200}, {200, 200});
  EXPECT_TRUE(IsEnclosedBy(rect, encloser));
  vk::Rect2D copy = rect;
  impl::ClipToRect(&rect, encloser);
  EXPECT_EQ(rect, copy);
}

// This test ensures that Fuchsia-specific Vulkan functions
// are properly loaded into the dynamic dispatcher whenever
// we are on a Fuchsia platform.
#ifdef VK_USE_PLATFORM_FUCHSIA
using VKFunctionTest = ::testing::Test;
VK_TEST(VKFunctionTest, FuchsiaFunctionLoading) {
  auto escher = escher::test::GetEscher();
  auto vk_loader = escher->device()->dispatch_loader();

  EXPECT_TRUE(vk_loader.vkCreateBufferCollectionFUCHSIA);
}
#endif

}  // anonymous namespace
