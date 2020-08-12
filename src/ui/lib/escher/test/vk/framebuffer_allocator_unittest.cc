// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/framebuffer_allocator.h"

#include <vector>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/vk/impl/framebuffer.h"
#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 1024;

struct FramebufferTextures {
  TexturePtr color1;
  TexturePtr color2;
  TexturePtr depth;
};

std::vector<FramebufferTextures> MakeFramebufferTextures(
    Escher* escher, size_t count, uint32_t width, uint32_t height, uint32_t sample_count,
    vk::Format color_format1, vk::Format color_format2, vk::Format depth_format) {
  std::vector<FramebufferTextures> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    FramebufferTextures textures;
    if (color_format1 != vk::Format::eUndefined) {
      textures.color1 =
          escher->NewAttachmentTexture(color_format1, width, height, 1, vk::Filter::eNearest);
    }
    if (color_format2 != vk::Format::eUndefined) {
      textures.color2 =
          escher->NewAttachmentTexture(color_format2, width, height, 1, vk::Filter::eNearest);
    }
    if (depth_format != vk::Format::eUndefined) {
      textures.depth =
          escher->NewAttachmentTexture(depth_format, width, height, 1, vk::Filter::eNearest);
    }
    result.push_back(std::move(textures));
  }
  return result;
}

RenderPassInfo MakeRenderPassInfo(const FramebufferTextures& textures) {
  RenderPassInfo info;
  info.num_color_attachments = 0;
  if (textures.color1) {
    info.color_attachments[info.num_color_attachments++] = textures.color1;
  }
  if (textures.color2) {
    info.color_attachments[info.num_color_attachments++] = textures.color2;
  }
  info.depth_stencil_attachment = textures.depth;

  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  EXPECT_TRUE(info.Validate());

  return info;
}

std::vector<impl::FramebufferPtr> ObtainFramebuffers(
    impl::FramebufferAllocator* allocator, const std::vector<FramebufferTextures>& textures) {
  std::vector<impl::FramebufferPtr> result;
  result.reserve(textures.size());
  for (auto& texs : textures) {
    result.push_back(allocator->ObtainFramebuffer(MakeRenderPassInfo(texs), true));
  }
  return result;
}

using FramebufferAllocatorTest = test::TestWithVkValidationLayer;

VK_TEST_F(FramebufferAllocatorTest, Basic) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  std::set<vk::Format> supported_depth_formats =
      escher->device()->caps().GetAllMatchingDepthStencilFormats(
          {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint});
  bool d24_supported =
      supported_depth_formats.find(vk::Format::eD24UnormS8Uint) != supported_depth_formats.end();
  bool d32_supported =
      supported_depth_formats.find(vk::Format::eD32SfloatS8Uint) != supported_depth_formats.end();

  // Create a pair of each of three types of framebuffers.
  auto textures_2colors_D24 = MakeFramebufferTextures(
      escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm, vk::Format::eB8G8R8A8Unorm,
      d24_supported ? vk::Format::eD24UnormS8Uint : vk::Format::eUndefined);
  auto textures_2colors_D32 = MakeFramebufferTextures(
      escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm, vk::Format::eB8G8R8A8Unorm,
      d32_supported ? vk::Format::eD32SfloatS8Uint : vk::Format::eUndefined);
  auto textures_1color_D32 = MakeFramebufferTextures(
      escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm, vk::Format::eUndefined,
      d32_supported ? vk::Format::eD32SfloatS8Uint : vk::Format::eUndefined);

  auto framebuffers_2colors_D24 = ObtainFramebuffers(&allocator, textures_2colors_D24);
  auto framebuffers_2colors_D32 = ObtainFramebuffers(&allocator, textures_2colors_D32);
  auto framebuffers_1color_D32 = ObtainFramebuffers(&allocator, textures_1color_D32);
  ASSERT_TRUE(framebuffers_2colors_D24[0] && framebuffers_2colors_D24[1]);
  ASSERT_TRUE(framebuffers_2colors_D32[0] && framebuffers_2colors_D32[1]);
  ASSERT_TRUE(framebuffers_1color_D32[0] && framebuffers_1color_D32[1]);

  // Each pair should have two different Framebuffers which share the same
  // RenderPass.
  EXPECT_NE(framebuffers_2colors_D24[0], framebuffers_2colors_D24[1]);
  EXPECT_EQ(framebuffers_2colors_D24[0]->render_pass(), framebuffers_2colors_D24[1]->render_pass());
  EXPECT_NE(framebuffers_2colors_D32[0], framebuffers_2colors_D32[1]);
  EXPECT_EQ(framebuffers_2colors_D32[0]->render_pass(), framebuffers_2colors_D32[1]->render_pass());
  EXPECT_NE(framebuffers_1color_D32[0], framebuffers_1color_D32[1]);
  EXPECT_EQ(framebuffers_1color_D32[0]->render_pass(), framebuffers_1color_D32[1]->render_pass());

  // If either D32 or D24 format is supported we will have different textures
  // for textures_2colors_D24 and textures_2colors_D32, so the render passes
  // will be different; otherwise they will be the same.
  // The rest pairs of Framebuffers should have different RenderPasses since the
  // color formats are different.
  if (d32_supported || d24_supported) {
    EXPECT_EQ(cache.size(), 3U);
    EXPECT_NE(framebuffers_2colors_D24[0]->render_pass(),
              framebuffers_2colors_D32[0]->render_pass());
  } else {
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_EQ(framebuffers_2colors_D24[0]->render_pass(),
              framebuffers_2colors_D32[0]->render_pass());
  }
  EXPECT_NE(framebuffers_2colors_D24[0]->render_pass(), framebuffers_1color_D32[0]->render_pass());
  EXPECT_NE(framebuffers_2colors_D32[0]->render_pass(), framebuffers_1color_D32[0]->render_pass());

  // TODO(fxbug.dev/36827) Now Vulkan validation layer has a performance warning:
  //   [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ]
  //   Layout for color attachment is GENERAL but should be COLOR_ATTACHMENT_OPTIMAL.
  SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS();
}

// Specificially test that we can create render-passes/framebuffers with no depth attachment.
// This will overlap with the "Basic" test on targets which don't support depth attachments,
// but we want to test this on all targets.
VK_TEST_F(FramebufferAllocatorTest, BasicNoDepth) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  // Create a pair of each of two types of framebuffers.
  auto textures_2colors =
      MakeFramebufferTextures(escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm,
                              vk::Format::eB8G8R8A8Unorm, vk::Format::eUndefined);
  auto textures_1color =
      MakeFramebufferTextures(escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm,
                              vk::Format::eUndefined, vk::Format::eUndefined);

  auto framebuffers_2colors = ObtainFramebuffers(&allocator, textures_2colors);
  auto framebuffers_1color = ObtainFramebuffers(&allocator, textures_1color);
  ASSERT_TRUE(framebuffers_2colors[0] && framebuffers_2colors[1]);
  ASSERT_TRUE(framebuffers_1color[0] && framebuffers_1color[1]);

  // Each pair should have two different Framebuffers which share the same
  // RenderPass.
  EXPECT_NE(framebuffers_2colors[0], framebuffers_2colors[1]);
  EXPECT_EQ(framebuffers_2colors[0]->render_pass(), framebuffers_2colors[1]->render_pass());
  EXPECT_NE(framebuffers_1color[0], framebuffers_1color[1]);
  EXPECT_EQ(framebuffers_1color[0]->render_pass(), framebuffers_1color[1]->render_pass());
  EXPECT_NE(framebuffers_2colors[0]->render_pass(), framebuffers_1color[0]->render_pass());

  // TODO(fxbug.dev/36827) Now Vulkan validation layer has a performance warning:
  //   [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ]
  //   Layout for color attachment is GENERAL but should be COLOR_ATTACHMENT_OPTIMAL.
  SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS();
}

// Test that we can create render-passes/framebuffers with no color attachment, only a depth
// attachment.  This is useful for e.g. rendering shadow maps.
VK_TEST_F(FramebufferAllocatorTest, DepthOnly) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  std::set<vk::Format> supported_depth_formats =
      escher->device()->caps().GetAllMatchingDepthStencilFormats(
          {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint});
  if (supported_depth_formats.empty()) {
    FX_LOGS(WARNING) << "No depth formats supported, skipping test.";
    return;
  }
  vk::Format supported_depth_format = *(supported_depth_formats.begin());

  // Create a pair of each of three types of framebuffers.
  auto textures = MakeFramebufferTextures(escher, 2, kWidth, kHeight, 1, vk::Format::eUndefined,
                                          vk::Format::eUndefined, supported_depth_format);

  auto framebuffers = ObtainFramebuffers(&allocator, textures);

  // Each pair should have two different Framebuffers which share the same
  // RenderPass.
  ASSERT_TRUE(framebuffers[0] && framebuffers[1]);
  EXPECT_NE(framebuffers[0], framebuffers[1]);
  EXPECT_EQ(framebuffers[0]->render_pass(), framebuffers[1]->render_pass());
}

VK_TEST_F(FramebufferAllocatorTest, CacheReclamation) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  // Make a single set of textures (depth and 2 color attachments) that will be
  // used to make a framebuffer.
  auto depth_format_result = escher->device()->caps().GetMatchingDepthFormat();
  vk::Format depth_format = depth_format_result.value;
  if (depth_format_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "No depth stencil format is supported on this device.";
    depth_format = vk::Format::eUndefined;
  }

  auto textures = MakeFramebufferTextures(escher, 1, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm,
                                          vk::Format::eB8G8R8A8Unorm, vk::Format::eUndefined);
  auto framebuffer = ObtainFramebuffers(&allocator, textures);

  // Obtaining a Framebuffer using the same textures should result in the same
  // Framebuffer.
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... this should still be true on the following frame.
  allocator.BeginFrame();
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... in fact, Framebuffers should not be evicted from the cache as long as
  // the number of frames since last use is < kFramesUntilEviction.
  constexpr uint32_t kNotEnoughFramesForEviction = 4;
  for (uint32_t i = 0; i < kNotEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... but one more frame than that will cause a different Framebuffer to be
  // obtained from the allocator.
  constexpr uint32_t kJustEnoughFramesForEviction = kNotEnoughFramesForEviction + 1;
  for (uint32_t i = 0; i < kJustEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_NE(framebuffer, ObtainFramebuffers(&allocator, textures));

  // TODO(fxbug.dev/36827) Now Vulkan validation layer has a performance warning:
  //   [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ]
  //   Layout for color attachment is GENERAL but should be COLOR_ATTACHMENT_OPTIMAL.
  SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS();
}

VK_TEST_F(FramebufferAllocatorTest, LazyRenderPassCreation) {
  auto escher = test::GetEscher();

  impl::RenderPassCache rp_cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &rp_cache);
  allocator.BeginFrame();

  // Make a single set of textures (depth and 2 color attachments) that will be
  // used to make a framebuffer.
  auto depth_format_result = escher->device()->caps().GetMatchingDepthFormat();
  vk::Format depth_format = depth_format_result.value;
  if (depth_format_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "No depth stencil format is supported on this device.";
    depth_format = vk::Format::eUndefined;
  }

  auto textures_bgra =
      MakeFramebufferTextures(escher, 2, kWidth, kHeight, 1, vk::Format::eB8G8R8A8Unorm,
                              vk::Format::eB8G8R8A8Unorm, vk::Format::eUndefined);

  auto textures_rgba =
      MakeFramebufferTextures(escher, 1, kWidth, kHeight, 1, vk::Format::eR8G8B8A8Unorm,
                              vk::Format::eR8G8B8A8Unorm, vk::Format::eUndefined);

  RenderPassInfo rpi_bgra0 = MakeRenderPassInfo(textures_bgra[0]);
  RenderPassInfo rpi_bgra1 = MakeRenderPassInfo(textures_bgra[1]);
  RenderPassInfo rpi_rgba0 = MakeRenderPassInfo(textures_rgba[0]);

  // No framebuffer obtained, because there is no render-pass yet.
  FX_LOGS(INFO) << "============= NOTE: Escher warnings expected";
  auto fb_bgra0 = allocator.ObtainFramebuffer(rpi_bgra0, false);
  EXPECT_FALSE(fb_bgra0);
  EXPECT_EQ(0U, allocator.size());
  EXPECT_EQ(0U, rp_cache.size());
  FX_LOGS(INFO) << "============= NOTE: no additional Escher warnings are expected\n";

  // This time, we allow lazy render-pass creation.
  fb_bgra0 = allocator.ObtainFramebuffer(rpi_bgra0, true);
  EXPECT_TRUE(fb_bgra0);

  // We can find the same framebuffer again, regardless of whether lazy render-pass creation is
  // allowed.
  EXPECT_EQ(fb_bgra0, allocator.ObtainFramebuffer(rpi_bgra0, false));
  EXPECT_EQ(fb_bgra0, allocator.ObtainFramebuffer(rpi_bgra0, true));
  EXPECT_EQ(1U, allocator.size());
  EXPECT_EQ(1U, rp_cache.size());

  // We can also obtain a new framebuffer, even if we disallow lazy render-pass creation (since the
  // existing render-pass will be found/used again).
  auto fb_bgra1 = allocator.ObtainFramebuffer(rpi_bgra1, false);
  EXPECT_TRUE(fb_bgra1);
  EXPECT_NE(fb_bgra0, fb_bgra1);
  EXPECT_EQ(2U, allocator.size());
  EXPECT_EQ(1U, rp_cache.size());
  EXPECT_EQ(fb_bgra0->render_pass(), fb_bgra1->render_pass());

  // Using an incompatible RenderPassInfo, disabling lazy render-pass creation means that we can't
  // obtain a framebuffer.
  FX_LOGS(INFO) << "============= NOTE: Escher warnings expected";
  auto fb_rgba0 = allocator.ObtainFramebuffer(rpi_rgba0, false);
  EXPECT_FALSE(fb_rgba0);
  EXPECT_EQ(2U, allocator.size());
  EXPECT_EQ(1U, rp_cache.size());
  FX_LOGS(INFO) << "============= NOTE: no additional Escher warnings are expected\n";

  // And of course, enabling lazy render-pass creation will allow us to obtain a framebuffer.
  fb_rgba0 = allocator.ObtainFramebuffer(rpi_rgba0, true);
  EXPECT_TRUE(fb_rgba0);
  EXPECT_EQ(3U, allocator.size());
  EXPECT_EQ(2U, rp_cache.size());
  EXPECT_NE(fb_rgba0->render_pass(), fb_bgra0->render_pass());
  EXPECT_NE(fb_rgba0->render_pass(), fb_bgra1->render_pass());

  // TODO(fxbug.dev/36827) Now Vulkan validation layer has a performance warning:
  //   [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ]
  //   Layout for color attachment is GENERAL but should be COLOR_ATTACHMENT_OPTIMAL.
  SUPPRESS_VK_VALIDATION_PERFORMANCE_WARNINGS();
}

}  // anonymous namespace
