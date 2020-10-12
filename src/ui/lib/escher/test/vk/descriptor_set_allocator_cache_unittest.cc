// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator_cache.h"

#include "gtest/gtest.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"

namespace {
using namespace escher;
using namespace escher::impl;

namespace {

// Select a proper format and filter pair supported by physical device.
std::pair<vk::Format, vk::Filter> SelectSupportedFormatAndFilter(Escher* escher) {
  // We use eNearest as filter since it doesn't depend on any format
  // features specific to physical devices.
  const auto kFilter = vk::Filter::eNearest;

  // For formats, we prefer YUV formats if any of them are supported; otherwise
  // we just choose a normal RGB format.
  const vk::Format kYuvFormats[] = {vk::Format::eG8B8G8R8422Unorm,
                                    vk::Format::eG8B8R82Plane420Unorm,
                                    vk::Format::eG8B8R83Plane420Unorm};
  for (const auto yuv_format : kYuvFormats) {
    if (impl::IsYuvConversionSupported(escher->vk_physical_device(), yuv_format)) {
      return {yuv_format, kFilter};
    }
  }
  return {vk::Format::eR8G8B8Srgb, kFilter};
}
}  // namespace

VK_TEST(DescriptorSetAllocatorCache, LazyCaching) {
  auto escher = escher::test::GetEscher();
  DescriptorSetAllocatorCache cache(escher->vk_device());

  DescriptorSetLayout layout1 = {};
  layout1.sampled_image_mask = 0x5;
  layout1.storage_image_mask = 0x2;
  layout1.input_attachment_mask = 0x8;
  auto a1 = cache.ObtainDescriptorSetAllocator(layout1, nullptr);
  auto a2 = cache.ObtainDescriptorSetAllocator(layout1, nullptr);
  EXPECT_EQ(a1, a2);
  EXPECT_EQ(1U, cache.size());

  DescriptorSetLayout layout2 = {};
  layout1.sampled_image_mask = 0x5;
  layout1.storage_image_mask = 0x2;
  layout1.input_attachment_mask = 0x10;
  auto a3 = cache.ObtainDescriptorSetAllocator(layout2, nullptr);
  EXPECT_NE(a1, a3);
  EXPECT_EQ(2U, cache.size());

  auto [format, filter] = SelectSupportedFormatAndFilter(escher);
  auto sampler = fxl::MakeRefCounted<Sampler>(escher->resource_recycler(), format, filter, true);
  auto a4 = cache.ObtainDescriptorSetAllocator(layout1, sampler);
  auto a5 = cache.ObtainDescriptorSetAllocator(layout1, sampler);
  EXPECT_NE(a1, a4);
  EXPECT_EQ(a4, a5);
  EXPECT_EQ(3U, cache.size());
}

VK_TEST(DescriptorSetAllocatorCache, ClearsReleasedDescriptorSetAllocator) {
  auto escher = escher::test::GetEscher();
  DescriptorSetAllocatorCache cache(escher->vk_device());

  DescriptorSetLayout layout1 = {};
  layout1.sampled_image_mask = 0x5;
  layout1.storage_image_mask = 0x2;
  layout1.input_attachment_mask = 0x8;
  auto a1 = cache.ObtainDescriptorSetAllocator(layout1, nullptr);
  EXPECT_EQ(1U, cache.size());

  a1.reset();
  cache.BeginFrame();
  EXPECT_EQ(0U, cache.size());
}

}  // namespace
